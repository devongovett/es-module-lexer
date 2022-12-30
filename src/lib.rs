use bumpalo::Bump;
use core::alloc::Layout;
use std::{ffi::c_void, marker::PhantomData, mem::MaybeUninit, ptr};

type Allocate = unsafe extern "C" fn(bytes: u32, user_data: *mut c_void) -> *mut c_void;
extern "C" {
  fn parse2(ptr: *const u8, len: u32, alloc: Allocate, user_data: *mut c_void, result: *mut ParseResult) -> bool;
}

#[repr(C)]
pub struct Import {
  start: *const u8,
  end: *const u8,
  statement_start: *const u8,
  statement_end: *const u8,
  assert_index: *const u8,
  dynamic: *const u8,
  safe: bool,
  next: *const Import,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ImportKind {
  Standard,
  DynamicString,
  DynamicExpression,
  Meta,
}

impl Import {
  pub fn specifier(&self) -> &str {
    unsafe {
      std::str::from_utf8_unchecked(std::slice::from_raw_parts(
        self.start,
        self.end as usize - self.start as usize,
      ))
    }
  }

  pub fn statement(&self) -> &str {
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

impl NextPtr for Import {
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
struct ParseResult {
  first_import: *const Import,
  first_export: *const Export,
  parse_error: u32,
  facade: bool,
  has_module_syntax: bool,
}

pub struct LexResult {
  bump: Bump,
  first_import: *const Import,
  first_export: *const Export,
}

impl LexResult {
  pub fn imports(&self) -> ResultIter<Import> {
    ResultIter {
      ptr: self.first_import,
      lifetime: PhantomData,
    }
  }

  pub fn exports(&self) -> ResultIter<Export> {
    ResultIter {
      ptr: self.first_export,
      lifetime: PhantomData,
    }
  }
}

trait NextPtr {
  fn next(&self) -> *const Self;
}

pub struct ResultIter<'a, T> {
  ptr: *const T,
  lifetime: PhantomData<&'a T>,
}

impl<'a, T: NextPtr> Iterator for ResultIter<'a, T> {
  type Item = &'a T;

  fn next(&mut self) -> Option<Self::Item> {
    if self.ptr.is_null() {
      return None;
    }

    let item = unsafe { &*self.ptr };
    self.ptr = item.next();
    Some(item)
  }
}

unsafe extern "C" fn alloc(bytes: u32, user_data: *mut c_void) -> *mut c_void {
  let bump: &mut Bump = &mut *(user_data as *mut Bump);
  let align = std::mem::align_of::<usize>();
  let layout = Layout::from_size_align_unchecked(bytes as usize, align);
  bump.alloc_layout(layout).as_ptr() as *mut c_void
}

pub unsafe fn lex(code: &str) -> Result<LexResult, usize> {
  let code_ptr = code.as_ptr();
  let mut res = LexResult {
    bump: Bump::new(),
    first_import: ptr::null(),
    first_export: ptr::null(),
  };
  let mut result: ParseResult = MaybeUninit::zeroed().assume_init();
  let success = parse2(
    code_ptr,
    code.len() as u32,
    alloc,
    &mut res.bump as *mut Bump as *mut c_void,
    &mut result as *mut ParseResult,
  );

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
  fn it_works() {
    let res = unsafe {
      lex(
        r#"import foo from "bar";
        import bar from "foo";
        export * as foo from "yoooo";
        export {test as hi};
        
        console.log(import("./hi"), import(hi), import.meta.dofih)"#,
      )
    }
    .unwrap();
    for import in res.imports() {
      println!("IMPORT {} {:?}", import.specifier(), import.kind());
    }

    for export in res.exports() {
      println!("EXPORT {:?} {:?}", export.exported(), export.local())
    }
  }
}
