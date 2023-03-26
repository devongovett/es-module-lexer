use bumpalo::Bump;
use core::alloc::Layout;
use std::{borrow::Cow, ffi::c_void, marker::PhantomData, mem::MaybeUninit, ptr, sync::atomic::AtomicPtr};

type Allocate = unsafe extern "C" fn(bytes: u32, user_data: *mut c_void) -> *mut c_void;
extern "C" {
  fn parse2(ptr: *const u8, len: u32, alloc: Allocate, user_data: *mut c_void, result: *mut ParseResult) -> bool;
}

#[repr(C)]
pub struct Import<'a> {
  start: *const u8,
  end: *const u8,
  statement_start: *const u8,
  statement_end: *const u8,
  assert_index: *const u8,
  dynamic: *const u8,
  safe: bool,
  next: *const Import<'a>,
  phantom: PhantomData<&'a ()>,
}

unsafe impl<'a> Send for Import<'a> {}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ImportKind {
  Standard,
  DynamicString,
  DynamicExpression,
  Meta,
}

impl<'a> Import<'a> {
  pub fn specifier(&self) -> Cow<'a, str> {
    let (start, end) = if self.kind() == ImportKind::DynamicString {
      unsafe { (self.start.offset(1), self.end.offset(-1)) }
    } else {
      (self.start, self.end)
    };

    let s =
      unsafe { std::str::from_utf8_unchecked(std::slice::from_raw_parts(start, end as usize - start as usize)) };
    if matches!(self.kind(), ImportKind::Standard | ImportKind::DynamicString) {
      unescape(s).unwrap_or(Cow::Borrowed(s))
    } else {
      Cow::Borrowed(s)
    }
  }

  pub fn statement(&self) -> &'a str {
    unsafe {
      std::str::from_utf8_unchecked(std::slice::from_raw_parts(
        self.statement_start,
        self.statement_end as usize - self.statement_start as usize,
      ))
    }
  }

  pub fn kind(&self) -> ImportKind {
    if self.dynamic as usize == 1 {
      ImportKind::Standard
    } else if self.dynamic as usize == 2 {
      ImportKind::Meta
    } else if self.safe {
      ImportKind::DynamicString
    } else {
      ImportKind::DynamicExpression
    }
  }
}

fn unescape<'a>(s: &'a str) -> Result<Cow<'a, str>, ()> {
  let mut cow = Cow::Borrowed(s);
  let bytes = s.as_bytes();
  let mut i = 0;
  let mut si = 0;
  while i < bytes.len() {
    let start = i;
    let b = bytes[i];
    if b == b'\\' && i + 1 < bytes.len() {
      i += 1;
      let c = match bytes[i] {
        b'n' => '\n',
        b'r' => '\r',
        b't' => '\t',
        b'b' => 8 as char,
        b'v' => 11 as char,
        b'f' => 12 as char,
        b'\r' | b'\n' => {
          // Unicode new line characters after \ get removed from output in both
          // template literals and strings
          cow.to_mut().replace_range(si..si + 2, "");
          i += 1;
          continue;
        }
        b'x' => {
          i += 1;
          read_hex(bytes, &mut i, 2)?
        }
        b'u' if i + 1 < bytes.len() => {
          i += 1;
          if bytes[i] == b'{' {
            i += 1;
            if let Some(end) = bytes[i..].iter().position(|c| *c == b'}') {
              let res = read_hex(bytes, &mut i, end)?;
              i += 1;
              res
            } else {
              return Err(());
            }
          } else {
            read_hex(bytes, &mut i, 4)?
          }
        }
        c if c >= b'0' && c <= b'7' => {
          let mut total = c - b'0';
          i += 1;
          for _ in 0..2 {
            if i < bytes.len() && bytes[i] >= b'0' && bytes[i] <= b'7' {
              if let Some(t) = total.checked_mul(8).and_then(|t| t.checked_add(bytes[i] - b'0')) {
                total = t;
                i += 1;
              } else {
                break;
              }
            } else {
              break;
            }
          }
          if i < bytes.len() && (bytes[i] == b'8' || bytes[i] == b'9') {
            return Err(());
          }
          total as char
        }
        c => c as char,
      };

      let mut buf = [0; 4];
      let r = c.encode_utf8(&mut buf);
      cow.to_mut().replace_range(si..si + i - start, r);
      si += r.len();
    } else {
      si += 1;
      i += 1;
    }
  }

  Ok(cow)
}

fn read_hex(bytes: &[u8], pos: &mut usize, len: usize) -> Result<char, ()> {
  let start = *pos;
  let mut total: u32 = 0;
  let mut last_code = 0;
  for _ in 0..len {
    let code = bytes[*pos];

    if code == b'_' {
      if last_code == b'_' {
        return Err(());
      }
      last_code = code;
      *pos += 1;
      continue;
    }

    let val = if code >= b'a' {
      code - b'a' + 10
    } else if code >= b'A' {
      code - b'A' + 10
    } else if code >= b'0' && code <= b'9' {
      code - b'0'
    } else {
      break;
    };

    if val >= 16 {
      break;
    }

    last_code = code;
    total = total * 16 + (val as u32);
    *pos += 1;
  }

  if last_code == b'_' || *pos - start != len {
    return Err(());
  }

  Ok(unsafe { char::from_u32_unchecked(total) })
}

impl<'a> NextPtr for Import<'a> {
  fn next(&self) -> *const Self {
    self.next
  }
}

#[repr(C)]
pub struct Export {
  start: *const u8,
  end: *const u8,
  local_start: *const u8,
  local_end: *const u8,
  next: *const Export,
}

impl NextPtr for Export {
  fn next(&self) -> *const Self {
    self.next
  }
}

impl Export {
  pub fn exported(&self) -> &str {
    unsafe {
      std::str::from_utf8_unchecked(std::slice::from_raw_parts(
        self.start,
        self.end as usize - self.start as usize,
      ))
    }
  }

  pub fn local(&self) -> Option<&str> {
    if self.local_start.is_null() {
      return None;
    }

    unsafe {
      Some(std::str::from_utf8_unchecked(std::slice::from_raw_parts(
        self.local_start,
        self.local_end as usize - self.local_start as usize,
      )))
    }
  }
}

#[repr(C)]
struct ParseResult<'a> {
  first_import: *const Import<'a>,
  first_export: *const Export,
  parse_error: u32,
  facade: bool,
  has_module_syntax: bool,
}

pub struct LexResult<'a> {
  bump: Bump,
  first_import: *const Import<'a>,
  first_export: *const Export,
}

impl<'a> LexResult<'a> {
  pub fn imports(&'a self) -> ResultIter<'a, Import> {
    ResultIter {
      ptr: AtomicPtr::new(self.first_import as *mut Import),
      lifetime: PhantomData,
    }
  }

  pub fn exports(&self) -> ResultIter<Export> {
    ResultIter {
      ptr: AtomicPtr::new(self.first_export as *mut Export),
      lifetime: PhantomData,
    }
  }
}

trait NextPtr {
  fn next(&self) -> *const Self;
}

pub struct ResultIter<'a, T> {
  ptr: AtomicPtr<T>,
  lifetime: PhantomData<&'a T>,
}

unsafe impl<'a, T: Send> Send for ResultIter<'a, T> {}

impl<'a, T: NextPtr> Iterator for ResultIter<'a, T> {
  type Item = &'a mut T;

  fn next(&mut self) -> Option<Self::Item> {
    let ptr = self.ptr.load(std::sync::atomic::Ordering::Relaxed);
    if ptr.is_null() {
      return None;
    }

    let item = unsafe { &mut *ptr };
    self.ptr.store(item.next() as *mut T, std::sync::atomic::Ordering::Relaxed);
    Some(item)
  }
}

unsafe extern "C" fn alloc(bytes: u32, user_data: *mut c_void) -> *mut c_void {
  let bump: &mut Bump = &mut *(user_data as *mut Bump);
  let align = std::mem::align_of::<usize>();
  let layout = Layout::from_size_align_unchecked(bytes as usize, align);
  bump.alloc_layout(layout).as_ptr() as *mut c_void
}

pub fn lex<'a>(code: &'a str) -> Result<LexResult<'a>, usize> {
  let code_ptr = code.as_ptr();
  let mut res = LexResult {
    bump: Bump::new(),
    first_import: ptr::null(),
    first_export: ptr::null(),
  };
  let mut result: ParseResult = unsafe { MaybeUninit::zeroed().assume_init() };
  let success = unsafe {
    parse2(
      code_ptr,
      code.len() as u32,
      alloc,
      &mut res.bump as *mut Bump as *mut c_void,
      &mut result as *mut ParseResult,
    )
  };

  if success {
    res.first_import = result.first_import;
    res.first_export = result.first_export;
    return Ok(res);
  }

  return Err(result.parse_error as usize);
}

#[cfg(test)]
mod tests {
  use super::*;

  #[test]
  fn esm() {
    let res = lex(
      r#"
        import bar from "foo";
        export * as foo from "yoooo";
        export {test as hi};

        import("./bar");
        import('./baz');
        import(`./a`);
        import(hi);
        import('./test/' + foo);
        import(`./test/${foo}`);
      "#,
    )
    .unwrap();

    let imports: Vec<(Cow<'_, str>, ImportKind)> = res.imports().map(|i| (i.specifier(), i.kind())).collect();
    let imports: Vec<(&str, ImportKind)> = imports.iter().map(|(i, k)| (i.as_ref(), *k)).collect();
    assert_eq!(
      imports,
      vec![
        ("foo", ImportKind::Standard),
        ("yoooo", ImportKind::Standard),
        ("./bar", ImportKind::DynamicString),
        ("./baz", ImportKind::DynamicString),
        ("./a", ImportKind::DynamicString),
        ("hi", ImportKind::DynamicExpression),
        ("'./test/' + foo", ImportKind::DynamicExpression),
        ("`./test/${foo}`", ImportKind::DynamicExpression),
      ]
    );
  }

  #[test]
  fn requires() {
    let res = lex(
      r#"
      require("./foo");
      require('./bar');
      require(`./baz`);
      require(/* something*/ 'yo');
      require(
        'test'
      );
      require(hi);
      require('foo/' + bar);
      require(`foo/${bar}`);
      require('foo', bar);
      foo(require);
      foo.require(); // ignored
      let req = require;
      req('foo');
      var require = 2;
      require.cache; // ignored
      require.resolve('foo'); // ignored
      var required = 'hi'; // ignored
      var test_require = 'hi'; // ignored
      var x = {require: true}; // ignored
      var x = {require : true}; // ignored
      var x = {require(test) {}}; // ignored
      var x = {require};
      // require (ignored)
      /* require (ignored) */
      var req = true ? require : false; // TODO
      if (true) {
        require('bar');
      }
      if (typeof require !== 'undefined') {
        crypto = require('crypto');
      }
      require('./\u{20204}.js');
      require('./\x61\x62\x63.js');
      require('./\251.js');
      require('./foo\
.js');
    "#,
    )
    .unwrap();

    let imports: Vec<(Cow<'_, str>, ImportKind)> = res.imports().map(|i| (i.specifier(), i.kind())).collect();
    let imports: Vec<(&str, ImportKind)> = imports.iter().map(|(i, k)| (i.as_ref(), *k)).collect();
    assert_eq!(
      imports,
      vec![
        ("./foo", ImportKind::DynamicString),
        ("./bar", ImportKind::DynamicString),
        ("./baz", ImportKind::DynamicString),
        ("yo", ImportKind::DynamicString),
        ("test", ImportKind::DynamicString),
        ("hi", ImportKind::DynamicExpression),
        ("'foo/' + bar", ImportKind::DynamicExpression),
        ("`foo/${bar}`", ImportKind::DynamicExpression),
        ("'foo', bar", ImportKind::DynamicExpression),
        ("", ImportKind::DynamicExpression),
        ("", ImportKind::DynamicExpression),
        ("", ImportKind::DynamicExpression),
        ("", ImportKind::DynamicExpression),
        ("bar", ImportKind::DynamicString),
        ("", ImportKind::DynamicExpression),
        ("crypto", ImportKind::DynamicString),
        ("./𠈄.js", ImportKind::DynamicString),
        ("./abc.js", ImportKind::DynamicString),
        ("./©.js", ImportKind::DynamicString),
        ("./foo.js", ImportKind::DynamicString),
      ]
    );
  }
}
