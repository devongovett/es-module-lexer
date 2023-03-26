#include "lexer.h"
#include <stdio.h>
#include <string.h>

// NOTE: MESSING WITH THESE REQUIRES MANUAL ASM DICTIONARY CONSTRUCTION (via lexer.emcc.js base64 decoding)
static const char16_t XPORT[] = { 'x', 'p', 'o', 'r', 't' };
static const char16_t EQUIRE[] = { 'e', 'q', 'u', 'i', 'r', 'e' };
static const char16_t MPORT[] = { 'm', 'p', 'o', 'r', 't' };
static const char16_t LASS[] = { 'l', 'a', 's', 's' };
static const char16_t ROM[] = { 'r', 'o', 'm' };
static const char16_t ETA[] = { 'e', 't', 'a' };
static const char16_t SSERT[] = { 's', 's', 'e', 'r', 't' };
static const char16_t VO[] = { 'v', 'o' };
static const char16_t YIE[] = { 'y', 'i', 'e' };
static const char16_t DELE[] = { 'd', 'e', 'l', 'e' };
static const char16_t INSTAN[] = { 'i', 'n', 's', 't', 'a', 'n' };
static const char16_t TY[] = { 't', 'y' };
static const char16_t RETUR[] = { 'r', 'e', 't', 'u', 'r' };
static const char16_t DEBUGGE[] = { 'd', 'e', 'b', 'u', 'g', 'g', 'e' };
static const char16_t AWAI[] = { 'a', 'w', 'a', 'i' };
static const char16_t THR[] = { 't', 'h', 'r' };
static const char16_t WHILE[] = { 'w', 'h', 'i', 'l', 'e' };
static const char16_t FOR[] = { 'f', 'o', 'r' };
static const char16_t IF[] = { 'i', 'f' };
static const char16_t CATC[] = { 'c', 'a', 't', 'c' };
static const char16_t FINALL[] = { 'f', 'i', 'n', 'a', 'l', 'l' };
static const char16_t ELS[] = { 'e', 'l', 's' };
static const char16_t BREA[] = { 'b', 'r', 'e', 'a' };
static const char16_t CONTIN[] = { 'c', 'o', 'n', 't', 'i', 'n' };
static const char16_t SYNC[] = {'s', 'y', 'n', 'c'};
static const char16_t UNCTION[] = {'u', 'n', 'c', 't', 'i', 'o', 'n'};

#ifndef RUST_BUILD
bool parse2 (char16_t *source, uint32_t sourceLen, Allocator alloc, void *user_data, ParseResult *result);
bool parse () {
  return parse2(globalSource, sourceLen, NULL, NULL, &result);
}
#endif // RUST_BUILD

// Note: parsing is based on the _assumption_ that the source is already valid
bool parse2 (char16_t *source, uint32_t sourceLen, Allocator alloc, void *user_data, ParseResult *result) {
  // stack allocations
  // these are done here to avoid data section \0\0\0 repetition bloat
  // (while gzip fixes this, still better to have ~10KiB ungzipped over ~20KiB)
  OpenToken openTokenStack_[1024];
  Import* dynamicImportStack_[512];

  result->facade = true;
  result->hasModuleSyntax = false;

  State state = {
    .dynamicImportStackDepth = 0,
    .openTokenDepth = 0,
    .lastTokenPos = (char16_t*)EMPTY_CHAR,
    .lastSlashWasDivision = false,
    .has_error = false,
    .openTokenStack = &openTokenStack_[0],
    .dynamicImportStack = &dynamicImportStack_[0],
    .nextBraceIsClass = false,
    .source = source,
#ifdef RUST_BUILD
    .alloc = alloc,
#endif
    .user_data = user_data,
    .result = result,
  };

  state.pos = (char16_t*)(source - 1);
  char16_t ch = '\0';
  state.end = state.pos + sourceLen;

  // start with a pure "module-only" parser
  while (state.pos++ < state.end) {
    ch = *state.pos;

    if (ch == 32 || ch < 14 && ch > 8)
      continue;

    switch (ch) {
      case 'e':
        if (state.openTokenDepth == 0 && keywordStart(&state) && memcmp(state.pos + 1, &XPORT[0], 5 * sizeof(char16_t)) == 0) {
          tryParseExportStatement(&state);
          // export might have been a non-pure declaration
          if (!state.result->facade) {
            state.lastTokenPos = state.pos;
            goto mainparse;
          }
        }
        break;
      case 'i':
        if (keywordStart(&state) && memcmp(state.pos + 1, &MPORT[0], 5 * sizeof(char16_t)) == 0)
          tryParseImportStatement(&state);
        break;
      case 'r':
        tryParseRequire(&state);
        break;
      case ';':
        break;
      case '/': {
        char16_t next_ch = *(state.pos + 1);
        if (next_ch == '/') {
          lineComment(&state);
          // dont update lastToken
          continue;
        }
        else if (next_ch == '*') {
          blockComment(&state, true);
          // dont update lastToken
          continue;
        }
        // fallthrough
      }
      default:
        // as soon as we hit a non-module token, we go to main parser
        state.result->facade = false;
        state.pos--;
        goto mainparse; // oh yeahhh
    }
    state.lastTokenPos = state.pos;
  }

  if (state.has_error)
    return false;

  mainparse: while (state.pos++ < state.end) {
    ch = *state.pos;

    if (ch == 32 || ch < 14 && ch > 8)
      continue;

    switch (ch) {
      case 'e':
        if (state.openTokenDepth == 0 && keywordStart(&state) && memcmp(state.pos + 1, &XPORT[0], 5 * sizeof(char16_t)) == 0)
          tryParseExportStatement(&state);
        break;
      case 'i':
        if (keywordStart(&state) && memcmp(state.pos + 1, &MPORT[0], 5 * sizeof(char16_t)) == 0)
          tryParseImportStatement(&state);
        break;
      case 'r':
        tryParseRequire(&state);
        break;
      case 'c':
        if (keywordStart(&state) && memcmp(state.pos + 1, &LASS[0], 4 * sizeof(char16_t)) == 0 && isBrOrWs(*(state.pos + 5)))
          state.nextBraceIsClass = true;
        break;
      case '(':
        state.openTokenStack[state.openTokenDepth].token = AnyParen;
        state.openTokenStack[state.openTokenDepth++].pos = state.lastTokenPos;
        break;
      case ')':
        if (state.openTokenDepth == 0)
          return syntaxError(&state), false;
        state.openTokenDepth--;
        if (state.dynamicImportStackDepth > 0 && state.openTokenStack[state.openTokenDepth].token == ImportParen) {
          Import* cur_dynamic_import = state.dynamicImportStack[state.dynamicImportStackDepth - 1];
          if (cur_dynamic_import->end == 0)
            cur_dynamic_import->end = state.lastTokenPos + 1;
          cur_dynamic_import->statement_end = state.pos + 1;
          state.dynamicImportStackDepth--;
        }
        break;
      case '{':
        // dynamic import followed by { is not a dynamic import (so remove)
        // this is a sneaky way to get around { import () {} } v { import () }
        // block / object ambiguity without a parser (assuming source is valid)
        if (*state.lastTokenPos == ')' && state.import_write_head && state.import_write_head->end == state.lastTokenPos) {
          state.import_write_head = state.import_write_head_last;
          if (state.import_write_head)
            state.import_write_head->next = NULL;
          else
            state.result->first_import = NULL;
        }
        state.openTokenStack[state.openTokenDepth].token = state.nextBraceIsClass ? ClassBrace : AnyBrace;
        state.openTokenStack[state.openTokenDepth++].pos = state.lastTokenPos;
        state.nextBraceIsClass = false;
        break;
      case '}':
        if (state.openTokenDepth == 0)
          return syntaxError(&state), false;
        if (state.openTokenStack[--state.openTokenDepth].token == TemplateBrace) {
          templateString(&state);
        }
        break;
      case '\'':
        stringLiteral(&state, ch);
        break;
      case '"':
        stringLiteral(&state, ch);
        break;
      case '/': {
        char16_t next_ch = *(state.pos + 1);
        if (next_ch == '/') {
          lineComment(&state);
          // dont update lastToken
          continue;
        }
        else if (next_ch == '*') {
          blockComment(&state, true);
          // dont update lastToken
          continue;
        }
        else {
          // Division / regex ambiguity handling based on checking backtrack analysis of:
          // - what token came previously (lastToken)
          // - if a closing brace or paren, what token came before the corresponding
          //   opening brace or paren (lastOpenTokenIndex)
          char16_t lastToken = *state.lastTokenPos;
          if (isExpressionPunctuator(lastToken) &&
              !(lastToken == '.' && (*(state.lastTokenPos - 1) >= '0' && *(state.lastTokenPos - 1) <= '9')) &&
              !(lastToken == '+' && *(state.lastTokenPos - 1) == '+') && !(lastToken == '-' && *(state.lastTokenPos - 1) == '-') ||
              lastToken == ')' && isParenKeyword(&state, state.openTokenStack[state.openTokenDepth].pos) ||
              lastToken == '}' && (isExpressionTerminator(&state, state.openTokenStack[state.openTokenDepth].pos) || state.openTokenStack[state.openTokenDepth].token == ClassBrace) ||
              isExpressionKeyword(&state, state.lastTokenPos) ||
              lastToken == '/' && state.lastSlashWasDivision ||
              !lastToken) {
            regularExpression(&state);
            state.lastSlashWasDivision = false;
          }
          else if (state.export_write_head != NULL && state.lastTokenPos >= state.export_write_head->start && state.lastTokenPos <= state.export_write_head->end) {
            // export default /some-regexp/
            regularExpression(&state);
            state.lastSlashWasDivision = false;
          }
          else {
            // Final check - if the last token was "break x" or "continue x"
            while (state.lastTokenPos > source && !isBrOrWsOrPunctuatorNotDot(*(--state.lastTokenPos)));
            if (isWsNotBr(*state.lastTokenPos)) {
              while (state.lastTokenPos > source && isWsNotBr(*(--state.lastTokenPos)));
              if (isBreakOrContinue(&state, state.lastTokenPos)) {
                regularExpression(&state);
                state.lastSlashWasDivision = false;
                break;
              }
            }
            state.lastSlashWasDivision = true;
          }
        }
        break;
      }
      case '`':
        state.openTokenStack[state.openTokenDepth].pos = state.lastTokenPos;
        state.openTokenStack[state.openTokenDepth++].token = Template;
        templateString(&state);
        break;
    }
    state.lastTokenPos = state.pos;
  }

  if (state.openTokenDepth || state.has_error || state.dynamicImportStackDepth)
    return false;

  // succeess
  return true;
}

void tryParseImportStatement (State *state) {
  char16_t* startPos = state->pos;

  state->pos += 6;

  char16_t ch = commentWhitespace(state, true);

  switch (ch) {
    // dynamic import
    case '(':
      state->openTokenStack[state->openTokenDepth].token = ImportParen;
      state->openTokenStack[state->openTokenDepth++].pos = state->pos;
      if (*state->lastTokenPos == '.')
        return;
      // dynamic import indicated by positive d
      char16_t* dynamicPos = state->pos;
      // try parse a string, to record a safe dynamic import string
      state->pos++;
      ch = commentWhitespace(state, true);
      addImport(state, startPos, state->pos, 0, dynamicPos);
      state->dynamicImportStack[state->dynamicImportStackDepth++] = state->import_write_head;
      if (ch == '\'' || ch == '"') {
        stringLiteral(state, ch);
      } else if (ch == '`') {
        state->openTokenStack[state->openTokenDepth].pos = state->pos;
        state->openTokenStack[state->openTokenDepth++].token = Template;
        templateString(state);
      } else {
        state->pos--;
        return;
      }
      state->pos++;
      char16_t* endPos = state->pos;
      ch = commentWhitespace(state, true);
      if (ch == ',') {
        state->pos++;
        ch = commentWhitespace(state, true);
        state->import_write_head->end = endPos;
        state->import_write_head->assert_index = state->pos;
        state->import_write_head->safe = true;
        state->pos--;
      }
      else if (ch == ')') {
        state->openTokenDepth--;
        state->import_write_head->end = endPos;
        state->import_write_head->statement_end = state->pos + 1;
        state->import_write_head->safe = true;
        state->dynamicImportStackDepth--;
      }
      else {
        state->pos--;
      }
      return;
    // import.meta
    case '.':
      state->pos++;
      ch = commentWhitespace(state, true);
      // import.meta indicated by d == -2
      if (ch == 'm' && memcmp(state->pos + 1, &ETA[0], 3 * sizeof(char16_t)) == 0 && (isSpread(state->lastTokenPos) || *state->lastTokenPos != '.'))
        addImport(state, startPos, startPos, state->pos + 4, IMPORT_META);
      return;

    default:
      // no space after "import" -> not an import keyword
      if (state->pos == startPos + 6) {
        state->pos--;
        break;
      }
    case '"':
    case '\'':
    case '*': {
      // import statement only permitted at base-level
      if (state->openTokenDepth != 0) {
        state->pos--;
        return;
      }
      while (state->pos < state->end) {
        ch = *state->pos;
        if (isQuote(ch)) {
          readImportString(state, startPos, ch);
          return;
        }
        state->pos++;
      }
      syntaxError(state);
      break;
    }

    case '{': {
      // import statement only permitted at base-level
      if (state->openTokenDepth != 0) {
        state->pos--;
        return;
      }

      while (state->pos < state->end) {
        ch = commentWhitespace(state, true);

        if (isQuote(ch)) {
          stringLiteral(state, ch);
        } else if (ch == '}') {
          state->pos++;
          break;
        }

        state->pos++;
      }

      ch = commentWhitespace(state, true);
      if (ch == 'f' && memcmp(state->pos + 1, &ROM[0], 3 * sizeof(char16_t)) != 0) {
        syntaxError(state);
        break;
      }

      state->pos += 4;
      ch = commentWhitespace(state, true);

      if (!isQuote(ch)) {
        return syntaxError(state);
      }

      readImportString(state, startPos, ch);

      break;
    }
  }
}

void tryParseRequire (State *state) {
  uint16_t* startPos = state->pos;
  // require('...')
  if (keywordStart(state) && memcmp(state->pos + 1, &EQUIRE[0], 6 * sizeof(char16_t)) == 0) {
    state->pos += 7;
    uint16_t ch = commentWhitespace(state, true);
    if (ch == '(') {
      state->openTokenStack[state->openTokenDepth].token = ImportParen;
      state->openTokenStack[state->openTokenDepth++].pos = state->pos;
      char16_t* dynamicPos = state->pos;
      state->pos++;
      ch = commentWhitespace(state, true);
      addImport(state, startPos, state->pos, 0, dynamicPos);
      state->dynamicImportStack[state->dynamicImportStackDepth++] = state->import_write_head;
      if (ch == '\'' || ch == '"') {
        stringLiteral(state, ch);
      } else if (ch == '`') {
        state->openTokenStack[state->openTokenDepth].pos = state->pos;
        state->openTokenStack[state->openTokenDepth++].token = Template;
        templateString(state);
      } else {
        state->pos--;
        return;
      }
      state->pos++;
      char16_t* endPos = state->pos;
      ch = commentWhitespace(state, true);
      if (ch == ')') {
        state->openTokenDepth--;
        state->import_write_head->end = endPos;
        state->import_write_head->statement_end = state->pos + 1;
        state->import_write_head->safe = true;
        state->dynamicImportStackDepth--;
      } else {
        state->pos--;
      }
      return;
    } else if (ch != ':' && ch != '.' && !isIdentifierChar(nextChar(state))) {
      addImport(state, startPos, state->pos, state->pos, state->pos);
    }
    state->pos = startPos;
  }
}

void tryParseExportStatement (State *state) {
  char16_t* sStartPos = state->pos;
  Export* prev_export_write_head = state->export_write_head;

  state->pos += 6;

  char16_t* curPos = state->pos;

  char16_t ch = commentWhitespace(state, true);

  if (state->pos == curPos && !isPunctuator(ch))
    return;

  if (ch == '{') {
    state->pos++;
    ch = commentWhitespace(state, true);
    while (true) {
      char16_t* startPos = state->pos;

      if (!isQuote(ch)) {
        ch = readToWsOrPunctuator(state, ch);
      }
      // export { "identifer" as } from
      // export { "@notid" as } from
      // export { "spa ce" as } from
      // export { " space" as } from
      // export { "space " as } from
      // export { "not~id" as } from
      // export { "%notid" as } from
      // export { "identifer" } from
      // export { "%notid" } from
      else {
        stringLiteral(state, ch);
        state->pos++;
      }

      char16_t* endPos = state->pos;
      commentWhitespace(state, true);
      ch = readExportAs(state, startPos, endPos);
      // ,
      if (ch == ',') {
        state->pos++;
        ch = commentWhitespace(state, true);
      }
      if (ch == '}')
        break;
      if (state->pos == startPos)
        return syntaxError(state);
      if (state->pos > state->end)
        return syntaxError(state);
    }
    state->result->hasModuleSyntax = true; // to handle "export {}"
    state->pos++;
    ch = commentWhitespace(state, true);
  }
  // export *
  // export * as X
  else if (ch == '*') {
    state->pos++;
    commentWhitespace(state, true);
    ch = readExportAs(state, state->pos, state->pos);
    ch = commentWhitespace(state, true);
  }
  else {
    state->result->facade = false;
    switch (ch) {
      // export default ...
      case 'd': {
        const char16_t* startPos = state->pos;
        state->pos += 7;
        ch = commentWhitespace(state, true);
        bool localName = false;
        switch (ch) {
          // export default async? function*? name? (){}
          case 'a':
            if (memcmp(state->pos + 1, &SYNC[0], 4 * sizeof(char16_t)) == 0 && isWsNotBr(*(state->pos + 5))) {
              state->pos += 5;
              ch = commentWhitespace(state, false);
            }
            else {
              break;
            }
          // fallthrough
          case 'f':
            if (memcmp(state->pos + 1, &UNCTION[0], 7 * sizeof(char16_t)) == 0 && (isBrOrWs(*(state->pos + 8)) || *(state->pos + 8) == '*' || *(state->pos + 8) == '(')) {
              state->pos += 8;
              ch = commentWhitespace(state, true);
              if (ch == '*') {
                state->pos++;
                ch = commentWhitespace(state, true);
              }
              if (ch == '(') {
                break;
              }
              localName = true;
            }
            break;
          case 'c':
            // export default class name? {}
            if (memcmp(state->pos + 1, &LASS[0], 4 * sizeof(char16_t)) == 0 && (isBrOrWs(*(state->pos + 5)) || *(state->pos + 5) == '{')) {
              state->pos += 5;
              ch = commentWhitespace(state, true);
              if (ch == '{') {
                break;
              }
              localName = true;
            }
            break;
        }
        if (localName) {
          const char16_t* localStartPos = state->pos;
          readToWsOrPunctuator(state, ch);
          if (state->pos > localStartPos) {
            addExport(state, startPos, startPos + 7, localStartPos, state->pos);
            state->pos--;
            return;
          }
        }
        addExport(state, startPos, startPos + 7, NULL, NULL);
        state->pos = (char16_t*)(startPos + 6);
        return;
      }
      // export async? function*? name () {
      case 'a':
        state->pos += 5;
        commentWhitespace(state, false);
      // fallthrough
      case 'f':
        state->pos += 8;
        ch = commentWhitespace(state, true);
        if (ch == '*') {
          state->pos++;
          ch = commentWhitespace(state, true);
        }
        const char16_t* startPos = state->pos;
        ch = readToWsOrPunctuator(state, ch);
        addExport(state, startPos, state->pos, startPos, state->pos);
        state->pos--;
        return;

      // export class name ...
      case 'c':
        if (memcmp(state->pos + 1, &LASS[0], 4 * sizeof(char16_t)) == 0 && isBrOrWsOrPunctuatorNotDot(*(state->pos + 5))) {
          state->pos += 5;
          ch = commentWhitespace(state, true);
          const char16_t* startPos = state->pos;
          ch = readToWsOrPunctuator(state, ch);
          addExport(state, startPos, state->pos, startPos, state->pos);
          state->pos--;
          return;
        }
        state->pos += 2;
      // fallthrough

      // export var/let/const name = ...(, name = ...)+
      case 'v':
      case 'l':
        // simple declaration lexing only handles names. Any syntax after variable equals is skipped
        // (export var p = function () { ... }, q = 5 skips "q")
        state->pos += 3;
        state->result->facade = false;
        ch = commentWhitespace(state, true);
        startPos = state->pos;
        ch = readToWsOrPunctuator(state, ch);
        // very basic destructuring support only of the singular form:
        //   export const { a, b, ...c }
        // without aliasing, nesting or defaults
        bool destructuring = ch == '{' || ch == '[';
        const char16_t* destructuringPos = state->pos;
        if (destructuring) {
          state->pos += 1;
          ch = commentWhitespace(state, true);
          startPos = state->pos;
          ch = readToWsOrPunctuator(state, ch);
        }
        do {
          if (state->pos == startPos)
            break;
          addExport(state, startPos, state->pos, startPos, state->pos);
          ch = commentWhitespace(state, true);
          if (destructuring && (ch == '}' || ch == ']')) {
            destructuring = false;
            break;
          }
          if (ch != ',') {
            state->pos -= 1;
            break;
          }
          state->pos++;
          ch = commentWhitespace(state, true);
          startPos = state->pos;
          // internal destructurings unsupported
          if (ch == '{' || ch == '[') {
            state->pos -= 1;
            break;
          }
          ch = readToWsOrPunctuator(state, ch);
        } while (true);
        // if stuck inside destructuring syntax, backtrack
        if (destructuring) {
          state->pos = (char16_t*)destructuringPos - 1;
        }
        return;

      default:
        return;
    }
  }

  // from ...
  if (ch == 'f' && memcmp(state->pos + 1, &ROM[0], 3 * sizeof(char16_t)) == 0) {
    state->pos += 4;
    readImportString(state, sStartPos, commentWhitespace(state, true));

    // There were no local names.
    for (Export* exprt = prev_export_write_head == NULL ? state->result->first_export : prev_export_write_head->next; exprt != NULL; exprt = exprt->next) {
      exprt->local_start = exprt->local_end = NULL;
    }
  }
  else {
    state->pos--;
  }
}

char16_t readExportAs (State *state, char16_t* startPos, char16_t* endPos) {
  char16_t ch = *state->pos;
  char16_t* localStartPos = startPos == endPos ? NULL : startPos;
  char16_t* localEndPos = startPos == endPos ? NULL : endPos;

  if (ch == 'a') {
    state->pos += 2;
    ch = commentWhitespace(state, true);
    startPos = state->pos;

    if (!isQuote(ch)) {
      ch = readToWsOrPunctuator(state, ch);
    }
    // export { mod as "identifer" } from
    // export { mod as "@notid" } from
    // export { mod as "spa ce" } from
    // export { mod as " space" } from
    // export { mod as "space " } from
    // export { mod as "not~id" } from
    // export { mod as "%notid" } from
    else {
      stringLiteral(state, ch);
      state->pos++;
    }

    endPos = state->pos;

    ch = commentWhitespace(state, true);
  }

  if (state->pos != startPos)
    addExport(state, startPos, endPos, localStartPos, localEndPos);
  return ch;
}

void readImportString (State *state, const char16_t* ss, char16_t ch) {
  const char16_t* startPos = state->pos + 1;
  if (ch == '\'') {
    stringLiteral(state, ch);
  }
  else if (ch == '"') {
    stringLiteral(state, ch);
  }
  else {
    syntaxError(state);
    return;
  }
  addImport(state, ss, startPos, state->pos, STANDARD_IMPORT);
  state->pos++;
  ch = commentWhitespace(state, false);
  if (!(ch == 'a' && memcmp(state->pos + 1, &SSERT[0], 5 * sizeof(char16_t)) == 0) && !(ch == 'w' && *(state->pos + 1) == 'i' && *(state->pos + 2) == 't' && *(state->pos + 3) == 'h')) {
    state->pos--;
    return;
  }
  char16_t* assertIndex = state->pos;
  state->pos += ch == 'a' ? 6 : 4;
  ch = commentWhitespace(state, true);
  if (ch != '{') {
    state->pos = assertIndex;
    return;
  }
  const char16_t* assertStart = state->pos;
  do {
    state->pos++;
    ch = commentWhitespace(state, true);
    if (ch == '\'') {
      stringLiteral(state, ch);
      state->pos++;
      ch = commentWhitespace(state, true);
    }
    else if (ch == '"') {
      stringLiteral(state, ch);
      state->pos++;
      ch = commentWhitespace(state, true);
    }
    else {
      ch = readToWsOrPunctuator(state, ch);
    }
    if (ch != ':') {
      state->pos = assertIndex;
      return;
    }
    state->pos++;
    ch = commentWhitespace(state, true);
    if (ch == '\'') {
      stringLiteral(state, ch);
    }
    else if (ch == '"') {
      stringLiteral(state, ch);
    }
    else {
      state->pos = assertIndex;
      return;
    }
    state->pos++;
    ch = commentWhitespace(state, true);
    if (ch == ',') {
      state->pos++;
      ch = commentWhitespace(state, true);
      if (ch == '}')
        break;
      continue;
    }
    if (ch == '}')
      break;
    state->pos = assertIndex;
    return;
  } while (true);
  state->import_write_head->assert_index = assertStart;
  state->import_write_head->statement_end = state->pos + 1;
}

char16_t commentWhitespace (State *state, bool br) {
  char16_t ch;
  do {
    ch = *state->pos;
    if (ch == '/') {
      char16_t next_ch = *(state->pos + 1);
      if (next_ch == '/')
        lineComment(state);
      else if (next_ch == '*')
        blockComment(state, br);
      else
        return ch;
    }
    else if (br ? !isBrOrWs(ch) : !isWsNotBr(ch)) {
      return ch;
    }
  } while (state->pos++ < state->end);
  return ch;
}

void templateString (State *state) {
  while (state->pos++ < state->end) {
    char16_t ch = *state->pos;
    if (ch == '$' && *(state->pos + 1) == '{') {
      state->pos++;
      state->openTokenStack[state->openTokenDepth].token = TemplateBrace;
      state->openTokenStack[state->openTokenDepth++].pos = state->pos;
      return;
    }
    if (ch == '`') {
      if (state->openTokenStack[--state->openTokenDepth].token != Template)
        syntaxError(state);
      return;
    }
    if (ch == '\\')
      state->pos++;
  }
  syntaxError(state);
}

void blockComment (State *state, bool br) {
  state->pos++;
  while (state->pos++ < state->end) {
    char16_t ch = *state->pos;
    if (!br && isBr(ch))
      return;
    if (ch == '*' && *(state->pos + 1) == '/') {
      state->pos++;
      return;
    }
  }
}

void lineComment (State *state) {
  while (state->pos++ < state->end) {
    char16_t ch = *state->pos;
    if (ch == '\n' || ch == '\r')
      return;
  }
}

void stringLiteral (State *state, char16_t quote) {
  while (state->pos++ < state->end) {
    char16_t ch = *state->pos;
    if (ch == quote)
      return;
    if (ch == '\\') {
      ch = *++state->pos;
      if (ch == '\r' && *(state->pos + 1) == '\n')
        state->pos++;
    }
    else if (isBr(ch))
      break;
  }
  syntaxError(state);
}

char16_t regexCharacterClass (State *state) {
  while (state->pos++ < state->end) {
    char16_t ch = *state->pos;
    if (ch == ']')
      return ch;
    if (ch == '\\')
      state->pos++;
    else if (ch == '\n' || ch == '\r')
      break;
  }
  syntaxError(state);
  return '\0';
}

void regularExpression (State *state) {
  while (state->pos++ < state->end) {
    char16_t ch = *state->pos;
    if (ch == '/')
      return;
    if (ch == '[')
      ch = regexCharacterClass(state);
    else if (ch == '\\')
      state->pos++;
    else if (ch == '\n' || ch == '\r')
      break;
  }
  syntaxError(state);
}

char16_t readToWsOrPunctuator (State *state, char16_t ch) {
  do {
    if (isBrOrWs(ch) || isPunctuator(ch))
      return ch;
  } while (ch = *(++state->pos));
  return ch;
}

// Note: non-asii BR and whitespace checks omitted for perf / footprint
// if there is a significant user need this can be reconsidered
bool isBr (char16_t c) {
  return c == '\r' || c == '\n';
}

bool isWsNotBr (char16_t c) {
  return c == 9 || c == 11 || c == 12 || c == 32 || c == 160;
}

bool isBrOrWs (char16_t c) {
  return c > 8 && c < 14 || c == 32 || c == 160;
}

bool isBrOrWsOrPunctuatorNotDot (char16_t c) {
  return c > 8 && c < 14 || c == 32 || c == 160 || isPunctuator(c) && c != '.';
}

bool isBrOrWsOrPunctuatorOrSpreadNotDot (char16_t* c) {
  return *c > 8 && *c < 14 || *c == 32 || *c == 160 || isPunctuator(*c) && (isSpread(c) || *c != '.');
}

bool isSpread (char16_t* c) {
  return *c == '.' && *(c - 1) == '.' && *(c - 2) == '.';
}

bool isQuote (char16_t ch) {
  return ch == '\'' || ch == '"';
}

bool keywordStart (State *state) {
  return state->pos == state->source || isBrOrWsOrPunctuatorOrSpreadNotDot(state->pos - 1);
}

bool readPrecedingKeyword1 (State *state, char16_t* pos, char16_t c1) {
  if (pos < state->source) return false;
  return *pos == c1 && (pos == state->source || isBrOrWsOrPunctuatorNotDot(*(pos - 1)));
}

bool readPrecedingKeywordn (State *state, char16_t* pos, const char16_t* compare, size_t n) {
  if (pos - n + 1 < state->source) return false;
  return memcmp(pos - n + 1, compare, n * sizeof(char16_t)) == 0 && (pos - n + 1 == state->source || isBrOrWsOrPunctuatorOrSpreadNotDot(pos - n));
}

// Detects one of case, debugger, delete, do, else, in, instanceof, new,
//   return, throw, typeof, void, yield ,await
bool isExpressionKeyword (State *state, char16_t* pos) {
  switch (*pos) {
    case 'd':
      switch (*(pos - 1)) {
        case 'i':
          // void
          return readPrecedingKeywordn(state, pos - 2, &VO[0], 2);
        case 'l':
          // yield
          return readPrecedingKeywordn(state, pos - 2, &YIE[0], 3);
        default:
          return false;
      }
    case 'e':
      switch (*(pos - 1)) {
        case 's':
          switch (*(pos - 2)) {
            case 'l':
              // else
              return readPrecedingKeyword1(state, pos - 3, 'e');
            case 'a':
              // case
              return readPrecedingKeyword1(state, pos - 3, 'c');
            default:
              return false;
          }
        case 't':
          // delete
          return readPrecedingKeywordn(state, pos - 2, &DELE[0], 4);
        case 'u':
          // continue
          return readPrecedingKeywordn(state, pos - 2, &CONTIN[0], 6);
        default:
          return false;
      }
    case 'f':
      if (*(pos - 1) != 'o' || *(pos - 2) != 'e')
        return false;
      switch (*(pos - 3)) {
        case 'c':
          // instanceof
          return readPrecedingKeywordn(state, pos - 4, &INSTAN[0], 6);
        case 'p':
          // typeof
          return readPrecedingKeywordn(state, pos - 4, &TY[0], 2);
        default:
          return false;
      }
    case 'k':
      // break
      return readPrecedingKeywordn(state, pos - 1, &BREA[0], 4);
    case 'n':
      // in, return
      return readPrecedingKeyword1(state, pos - 1, 'i') || readPrecedingKeywordn(state, pos - 1, &RETUR[0], 5);
    case 'o':
      // do
      return readPrecedingKeyword1(state, pos - 1, 'd');
    case 'r':
      // debugger
      return readPrecedingKeywordn(state, pos - 1, &DEBUGGE[0], 7);
    case 't':
      // await
      return readPrecedingKeywordn(state, pos - 1, &AWAI[0], 4);
    case 'w':
      switch (*(pos - 1)) {
        case 'e':
          // new
          return readPrecedingKeyword1(state, pos - 2, 'n');
        case 'o':
          // throw
          return readPrecedingKeywordn(state, pos - 2, &THR[0], 3);
        default:
          return false;
      }
  }
  return false;
}

bool isParenKeyword (State *state, char16_t* curPos) {
  return readPrecedingKeywordn(state, curPos, &WHILE[0], 5) ||
      readPrecedingKeywordn(state, curPos, &FOR[0], 3) ||
      readPrecedingKeywordn(state, curPos, &IF[0], 2);
}

bool isPunctuator (char16_t ch) {
  // 23 possible punctuator endings: !%&()*+,-./:;<=>?[]^{}|~
  return ch == '!' || ch == '%' || ch == '&' ||
    ch > 39 && ch < 48 || ch > 57 && ch < 64 ||
    ch == '[' || ch == ']' || ch == '^' ||
    ch > 122 && ch < 127;
}

bool isExpressionPunctuator (char16_t ch) {
  // 20 possible expression endings: !%&(*+,-.:;<=>?[^{|~
  return ch == '!' || ch == '%' || ch == '&' ||
    ch > 39 && ch < 47 && ch != 41 || ch > 57 && ch < 64 ||
    ch == '[' || ch == '^' || ch > 122 && ch < 127 && ch != '}';
}

bool isBreakOrContinue (State *state, char16_t* curPos) {
  switch (*curPos) {
    case 'k':
      return readPrecedingKeywordn(state, curPos - 1, &BREA[0], 4);
    case 'e':
      if (*(curPos - 1) == 'u')
        return readPrecedingKeywordn(state, curPos - 2, &CONTIN[0], 6);
  }
  return false;
}

bool isExpressionTerminator (State *state, char16_t* curPos) {
  // detects:
  // => ; ) finally catch else class X
  // as all of these followed by a { will indicate a statement brace
  switch (*curPos) {
    case '>':
      return *(curPos - 1) == '=';
    case ';':
    case ')':
      return true;
    case 'h':
      return readPrecedingKeywordn(state, curPos - 1, &CATC[0], 4);
    case 'y':
      return readPrecedingKeywordn(state, curPos - 1, &FINALL[0], 6);
    case 'e':
      return readPrecedingKeywordn(state, curPos - 1, &ELS[0], 3);
  }
  return false;
}

// Identifier detection, ported from Acorn
// ## Character categories

// Big ugly regular expressions that match characters in the
// whitespace, identifier, and identifier-start categories. These
// are only applied when a character is found to actually have a
// code point above 128.
// Generated by `bin/generate-identifier-regex.js`.

bool isNonASCIIidentifierStartChar (uint32_t ch) {
  return ch == 0xaa || ch == 0xb5 || ch == 0xba || ch >= 0xc0 && ch <= 0xd6 || ch >= 0xd8 && ch <= 0xf6 || ch >= 0xf8 && ch <= 0x02c1 ||
      ch >= 0x02c6 && ch <= 0x02d1 || ch >= 0x02e0 && ch <= 0x02e4 || ch == 0x02ec || ch == 0x02ee || ch >= 0x0370 && ch <= 0x0374 || ch == 0x0376 || ch == 0x0377 || ch >= 0x037a && ch <= 0x037d || ch == 0x037f || ch == 0x0386 || ch >= 0x0388 && ch <= 0x038a || ch == 0x038c || ch >= 0x038e && ch <= 0x03a1 || ch >= 0x03a3 && ch <= 0x03f5 || ch >= 0x03f7 && ch <= 0x0481 || ch >= 0x048a && ch <= 0x052f || ch >= 0x0531 && ch <= 0x0556 || ch == 0x0559 || ch >= 0x0560 && ch <= 0x0588 || ch >= 0x05d0 && ch <= 0x05ea || ch >= 0x05ef && ch <= 0x05f2 || ch >= 0x0620 && ch <= 0x064a || ch == 0x066e || ch == 0x066f || ch >= 0x0671 && ch <= 0x06d3 || ch == 0x06d5 || ch == 0x06e5 || ch == 0x06e6 || ch == 0x06ee || ch == 0x06ef || ch >= 0x06fa && ch <= 0x06fc || ch == 0x06ff || ch == 0x0710 || ch >= 0x0712 && ch <= 0x072f || ch >= 0x074d && ch <= 0x07a5 || ch == 0x07b1 || ch >= 0x07ca && ch <= 0x07ea || ch == 0x07f4 || ch == 0x07f5 || ch == 0x07fa || ch >= 0x0800 && ch <= 0x0815 || ch == 0x081a || ch == 0x0824 || ch == 0x0828 || ch >= 0x0840 && ch <= 0x0858 || ch >= 0x0860 && ch <= 0x086a || ch >= 0x08a0 && ch <= 0x08b4 || ch >= 0x08b6 && ch <= 0x08bd || ch >= 0x0904 && ch <= 0x0939 || ch == 0x093d || ch == 0x0950 || ch >= 0x0958 && ch <= 0x0961 || ch >= 0x0971 && ch <= 0x0980 || ch >= 0x0985 && ch <= 0x098c || ch == 0x098f || ch == 0x0990 || ch >= 0x0993 && ch <= 0x09a8 || ch >= 0x09aa && ch <= 0x09b0 || ch == 0x09b2 || ch >= 0x09b6 && ch <= 0x09b9 || ch == 0x09bd || ch == 0x09ce || ch == 0x09dc || ch == 0x09dd || ch >= 0x09df && ch <= 0x09e1 || ch == 0x09f0 || ch == 0x09f1 || ch == 0x09fc || ch >= 0x0a05 && ch <= 0x0a0a || ch == 0x0a0f || ch == 0x0a10 || ch >= 0x0a13 && ch <= 0x0a28 || ch >= 0x0a2a && ch <= 0x0a30 || ch == 0x0a32 || ch == 0x0a33 || ch == 0x0a35 || ch == 0x0a36 || ch == 0x0a38 || ch == 0x0a39 || ch >= 0x0a59 && ch <= 0x0a5c || ch == 0x0a5e || ch >= 0x0a72 && ch <= 0x0a74 || ch >= 0x0a85 && ch <= 0x0a8d || ch >= 0x0a8f && ch <= 0x0a91 || ch >= 0x0a93 && ch <= 0x0aa8 || ch >= 0x0aaa && ch <= 0x0ab0 || ch == 0x0ab2 || ch == 0x0ab3 || ch >= 0x0ab5 && ch <= 0x0ab9 || ch == 0x0abd || ch == 0x0ad0 || ch == 0x0ae0 || ch == 0x0ae1 || ch == 0x0af9 || ch >= 0x0b05 && ch <= 0x0b0c || ch == 0x0b0f || ch == 0x0b10 || ch >= 0x0b13 && ch <= 0x0b28 || ch >= 0x0b2a && ch <= 0x0b30 || ch == 0x0b32 || ch == 0x0b33 || ch >= 0x0b35 && ch <= 0x0b39 || ch == 0x0b3d || ch == 0x0b5c || ch == 0x0b5d || ch >= 0x0b5f && ch <= 0x0b61 || ch == 0x0b71 || ch == 0x0b83 || ch >= 0x0b85 && ch <= 0x0b8a || ch >= 0x0b8e && ch <= 0x0b90 || ch >= 0x0b92 && ch <= 0x0b95 || ch == 0x0b99 || ch == 0x0b9a || ch == 0x0b9c || ch == 0x0b9e || ch == 0x0b9f || ch == 0x0ba3 || ch == 0x0ba4 || ch >= 0x0ba8 && ch <= 0x0baa || ch >= 0x0bae && ch <= 0x0bb9 || ch == 0x0bd0 || ch >= 0x0c05 && ch <= 0x0c0c || ch >= 0x0c0e && ch <= 0x0c10 || ch >= 0x0c12 && ch <= 0x0c28 || ch >= 0x0c2a && ch <= 0x0c39 || ch == 0x0c3d || ch >= 0x0c58 && ch <= 0x0c5a || ch == 0x0c60 || ch == 0x0c61 || ch == 0x0c80 || ch >= 0x0c85 && ch <= 0x0c8c || ch >= 0x0c8e && ch <= 0x0c90 || ch >= 0x0c92 && ch <= 0x0ca8 || ch >= 0x0caa && ch <= 0x0cb3 || ch >= 0x0cb5 && ch <= 0x0cb9 || ch == 0x0cbd || ch == 0x0cde || ch == 0x0ce0 || ch == 0x0ce1 || ch == 0x0cf1 || ch == 0x0cf2 || ch >= 0x0d05 && ch <= 0x0d0c || ch >= 0x0d0e && ch <= 0x0d10 || ch >= 0x0d12 && ch <= 0x0d3a || ch == 0x0d3d || ch == 0x0d4e || ch >= 0x0d54 && ch <= 0x0d56 || ch >= 0x0d5f && ch <= 0x0d61 || ch >= 0x0d7a && ch <= 0x0d7f || ch >= 0x0d85 && ch <= 0x0d96 || ch >= 0x0d9a && ch <= 0x0db1 || ch >= 0x0db3 && ch <= 0x0dbb || ch == 0x0dbd || ch >= 0x0dc0 && ch <= 0x0dc6 || ch >= 0x0e01 && ch <= 0x0e30 || ch == 0x0e32 || ch == 0x0e33 || ch >= 0x0e40 && ch <= 0x0e46 || ch == 0x0e81 || ch == 0x0e82 || ch == 0x0e84 || ch == 0x0e87 || ch == 0x0e88 || ch == 0x0e8a || ch == 0x0e8d || ch >= 0x0e94 && ch <= 0x0e97 || ch >= 0x0e99 && ch <= 0x0e9f || ch >= 0x0ea1 && ch <= 0x0ea3 || ch == 0x0ea5 || ch == 0x0ea7 || ch == 0x0eaa || ch == 0x0eab || ch >= 0x0ead && ch <= 0x0eb0 || ch == 0x0eb2 || ch == 0x0eb3 || ch == 0x0ebd || ch >= 0x0ec0 && ch <= 0x0ec4 || ch == 0x0ec6 || ch >= 0x0edc && ch <= 0x0edf || ch == 0x0f00 || ch >= 0x0f40 && ch <= 0x0f47 || ch >= 0x0f49 && ch <= 0x0f6c || ch >= 0x0f88 && ch <= 0x0f8c || ch >= 0x1000 && ch <= 0x102a || ch == 0x103f || ch >= 0x1050 && ch <= 0x1055 || ch >= 0x105a && ch <= 0x105d || ch == 0x1061 || ch == 0x1065 || ch == 0x1066 || ch >= 0x106e && ch <= 0x1070 || ch >= 0x1075 && ch <= 0x1081 || ch == 0x108e || ch >= 0x10a0 && ch <= 0x10c5 || ch == 0x10c7 || ch == 0x10cd || ch >= 0x10d0 && ch <= 0x10fa || ch >= 0x10fc && ch <= 0x1248 || ch >= 0x124a && ch <= 0x124d || ch >= 0x1250 && ch <= 0x1256 || ch == 0x1258 || ch >= 0x125a && ch <= 0x125d || ch >= 0x1260 && ch <= 0x1288 || ch >= 0x128a && ch <= 0x128d || ch >= 0x1290 && ch <= 0x12b0 || ch >= 0x12b2 && ch <= 0x12b5 || ch >= 0x12b8 && ch <= 0x12be || ch == 0x12c0 || ch >= 0x12c2 && ch <= 0x12c5 || ch >= 0x12c8 && ch <= 0x12d6 || ch >= 0x12d8 && ch <= 0x1310 || ch >= 0x1312 && ch <= 0x1315 || ch >= 0x1318 && ch <= 0x135a || ch >= 0x1380 && ch <= 0x138f || ch >= 0x13a0 && ch <= 0x13f5 || ch >= 0x13f8 && ch <= 0x13fd || ch >= 0x1401 && ch <= 0x166c || ch >= 0x166f && ch <= 0x167f || ch >= 0x1681 && ch <= 0x169a || ch >= 0x16a0 && ch <= 0x16ea || ch >= 0x16ee && ch <= 0x16f8 || ch >= 0x1700 && ch <= 0x170c || ch >= 0x170e && ch <= 0x1711 || ch >= 0x1720 && ch <= 0x1731 || ch >= 0x1740 && ch <= 0x1751 || ch >= 0x1760 && ch <= 0x176c || ch >= 0x176e && ch <= 0x1770 || ch >= 0x1780 && ch <= 0x17b3 || ch == 0x17d7 || ch == 0x17dc || ch >= 0x1820 && ch <= 0x1878 || ch >= 0x1880 && ch <= 0x18a8 || ch == 0x18aa || ch >= 0x18b0 && ch <= 0x18f5 || ch >= 0x1900 && ch <= 0x191e || ch >= 0x1950 && ch <= 0x196d || ch >= 0x1970 && ch <= 0x1974 || ch >= 0x1980 && ch <= 0x19ab || ch >= 0x19b0 && ch <= 0x19c9 || ch >= 0x1a00 && ch <= 0x1a16 || ch >= 0x1a20 && ch <= 0x1a54 || ch == 0x1aa7 || ch >= 0x1b05 && ch <= 0x1b33 || ch >= 0x1b45 && ch <= 0x1b4b || ch >= 0x1b83 && ch <= 0x1ba0 || ch == 0x1bae || ch == 0x1baf || ch >= 0x1bba && ch <= 0x1be5 || ch >= 0x1c00 && ch <= 0x1c23 || ch >= 0x1c4d && ch <= 0x1c4f || ch >= 0x1c5a && ch <= 0x1c7d || ch >= 0x1c80 && ch <= 0x1c88 || ch >= 0x1c90 && ch <= 0x1cba || ch >= 0x1cbd && ch <= 0x1cbf || ch >= 0x1ce9 && ch <= 0x1cec || ch >= 0x1cee && ch <= 0x1cf1 || ch == 0x1cf5 || ch == 0x1cf6 || ch >= 0x1d00 && ch <= 0x1dbf || ch >= 0x1e00 && ch <= 0x1f15 || ch >= 0x1f18 && ch <= 0x1f1d || ch >= 0x1f20 && ch <= 0x1f45 || ch >= 0x1f48 && ch <= 0x1f4d || ch >= 0x1f50 && ch <= 0x1f57 || ch == 0x1f59 || ch == 0x1f5b || ch == 0x1f5d || ch >= 0x1f5f && ch <= 0x1f7d || ch >= 0x1f80 && ch <= 0x1fb4 || ch >= 0x1fb6 && ch <= 0x1fbc || ch == 0x1fbe || ch >= 0x1fc2 && ch <= 0x1fc4 || ch >= 0x1fc6 && ch <= 0x1fcc || ch >= 0x1fd0 && ch <= 0x1fd3 || ch >= 0x1fd6 && ch <= 0x1fdb || ch >= 0x1fe0 && ch <= 0x1fec || ch >= 0x1ff2 && ch <= 0x1ff4 || ch >= 0x1ff6 && ch <= 0x1ffc || ch == 0x2071 || ch == 0x207f || ch >= 0x2090 && ch <= 0x209c || ch == 0x2102 || ch == 0x2107 || ch >= 0x210a && ch <= 0x2113 || ch == 0x2115 || ch >= 0x2118 && ch <= 0x211d || ch == 0x2124 || ch == 0x2126 || ch == 0x2128 || ch >= 0x212a && ch <= 0x2139 || ch >= 0x213c && ch <= 0x213f || ch >= 0x2145 && ch <= 0x2149 || ch == 0x214e || ch >= 0x2160 && ch <= 0x2188 || ch >= 0x2c00 && ch <= 0x2c2e || ch >= 0x2c30 && ch <= 0x2c5e || ch >= 0x2c60 && ch <= 0x2ce4 || ch >= 0x2ceb && ch <= 0x2cee || ch == 0x2cf2 || ch == 0x2cf3 || ch >= 0x2d00 && ch <= 0x2d25 || ch == 0x2d27 || ch == 0x2d2d || ch >= 0x2d30 && ch <= 0x2d67 || ch == 0x2d6f || ch >= 0x2d80 && ch <= 0x2d96 || ch >= 0x2da0 && ch <= 0x2da6 || ch >= 0x2da8 && ch <= 0x2dae || ch >= 0x2db0 && ch <= 0x2db6 || ch >= 0x2db8 && ch <= 0x2dbe || ch >= 0x2dc0 && ch <= 0x2dc6 || ch >= 0x2dc8 && ch <= 0x2dce || ch >= 0x2dd0 && ch <= 0x2dd6 || ch >= 0x2dd8 && ch <= 0x2dde || ch >= 0x3005 && ch <= 0x3007 || ch >= 0x3021 && ch <= 0x3029 || ch >= 0x3031 && ch <= 0x3035 || ch >= 0x3038 && ch <= 0x303c || ch >= 0x3041 && ch <= 0x3096 || ch >= 0x309b && ch <= 0x309f || ch >= 0x30a1 && ch <= 0x30fa || ch >= 0x30fc && ch <= 0x30ff || ch >= 0x3105 && ch <= 0x312f || ch >= 0x3131 && ch <= 0x318e || ch >= 0x31a0 && ch <= 0x31ba || ch >= 0x31f0 && ch <= 0x31ff || ch >= 0x3400 && ch <= 0x4db5 || ch >= 0x4e00 && ch <= 0x9fef || ch >= 0xa000 && ch <= 0xa48c || ch >= 0xa4d0 && ch <= 0xa4fd || ch >= 0xa500 && ch <= 0xa60c || ch >= 0xa610 && ch <= 0xa61f || ch == 0xa62a || ch == 0xa62b || ch >= 0xa640 && ch <= 0xa66e || ch >= 0xa67f && ch <= 0xa69d || ch >= 0xa6a0 && ch <= 0xa6ef || ch >= 0xa717 && ch <= 0xa71f || ch >= 0xa722 && ch <= 0xa788 || ch >= 0xa78b && ch <= 0xa7b9 || ch >= 0xa7f7 && ch <= 0xa801 || ch >= 0xa803 && ch <= 0xa805 || ch >= 0xa807 && ch <= 0xa80a || ch >= 0xa80c && ch <= 0xa822 || ch >= 0xa840 && ch <= 0xa873 || ch >= 0xa882 && ch <= 0xa8b3 || ch >= 0xa8f2 && ch <= 0xa8f7 || ch == 0xa8fb || ch == 0xa8fd || ch == 0xa8fe || ch >= 0xa90a && ch <= 0xa925 || ch >= 0xa930 && ch <= 0xa946 || ch >= 0xa960 && ch <= 0xa97c || ch >= 0xa984 && ch <= 0xa9b2 || ch == 0xa9cf || ch >= 0xa9e0 && ch <= 0xa9e4 || ch >= 0xa9e6 && ch <= 0xa9ef || ch >= 0xa9fa && ch <= 0xa9fe || ch >= 0xaa00 && ch <= 0xaa28 || ch >= 0xaa40 && ch <= 0xaa42 || ch >= 0xaa44 && ch <= 0xaa4b || ch >= 0xaa60 && ch <= 0xaa76 || ch == 0xaa7a || ch >= 0xaa7e && ch <= 0xaaaf || ch == 0xaab1 || ch == 0xaab5 || ch == 0xaab6 || ch >= 0xaab9 && ch <= 0xaabd || ch == 0xaac0 || ch == 0xaac2 || ch >= 0xaadb && ch <= 0xaadd || ch >= 0xaae0 && ch <= 0xaaea || ch >= 0xaaf2 && ch <= 0xaaf4 || ch >= 0xab01 && ch <= 0xab06 || ch >= 0xab09 && ch <= 0xab0e || ch >= 0xab11 && ch <= 0xab16 || ch >= 0xab20 && ch <= 0xab26 || ch >= 0xab28 && ch <= 0xab2e ||
      ch >= 0xab30 && ch <= 0xab5a || ch >= 0xab5c && ch <= 0xab65 || ch >= 0xab70 && ch <= 0xabe2 || ch >= 0xac00 && ch <= 0xd7a3 || ch >= 0xd7b0 && ch <= 0xd7c6 || ch >= 0xd7cb && ch <= 0xd7fb || ch >= 0xf900 && ch <= 0xfa6d || ch >= 0xfa70 && ch <= 0xfad9 || ch >= 0xfb00 && ch <= 0xfb06 || ch >= 0xfb13 && ch <= 0xfb17 || ch == 0xfb1d || ch >= 0xfb1f && ch <= 0xfb28 || ch >= 0xfb2a && ch <= 0xfb36 || ch >= 0xfb38 && ch <= 0xfb3c || ch == 0xfb3e || ch == 0xfb40 || ch == 0xfb41 || ch == 0xfb43 || ch == 0xfb44 || ch >= 0xfb46 && ch <= 0xfbb1 || ch >= 0xfbd3 && ch <= 0xfd3d || ch >= 0xfd50 && ch <= 0xfd8f || ch >= 0xfd92 && ch <= 0xfdc7 || ch >= 0xfdf0 && ch <= 0xfdfb || ch >= 0xfe70 && ch <= 0xfe74 || ch >= 0xfe76 && ch <= 0xfefc || ch >= 0xff21 && ch <= 0xff3a || ch >= 0xff41 && ch <= 0xff5a || ch >= 0xff66 && ch <= 0xffbe || ch >= 0xffc2 && ch <= 0xffc7 || ch >= 0xffca && ch <= 0xffcf || ch >= 0xffd2 && ch <= 0xffd7 || ch >= 0xffda && ch <= 0xffdc;
}

bool isNonASCIIidentifierChar (uint32_t ch) {
  return isNonASCIIidentifierStartChar(ch) || ch == 0x200c || ch == 0x200d || ch == 0xb7 || ch >= 0x0300 && ch <= 0x036f || ch == 0x0387 || ch >= 0x0483 && ch <= 0x0487 || ch >= 0x0591 && ch <= 0x05bd || ch == 0x05bf || ch == 0x05c1 || ch == 0x05c2 || ch == 0x05c4 || ch == 0x05c5 || ch == 0x05c7 || ch >= 0x0610 && ch <= 0x061a || ch >= 0x064b && ch <= 0x0669 || ch == 0x0670 || ch >= 0x06d6 && ch <= 0x06dc || ch >= 0x06df && ch <= 0x06e4 || ch == 0x06e7 || ch == 0x06e8 || ch >= 0x06ea && ch <= 0x06ed || ch >= 0x06f0 && ch <= 0x06f9 || ch == 0x0711 || ch >= 0x0730 && ch <= 0x074a || ch >= 0x07a6 && ch <= 0x07b0 || ch >= 0x07c0 && ch <= 0x07c9 || ch >= 0x07eb && ch <= 0x07f3 || ch == 0x07fd || ch >= 0x0816 && ch <= 0x0819 || ch >= 0x081b && ch <= 0x0823 || ch >= 0x0825 && ch <= 0x0827 || ch >= 0x0829 && ch <= 0x082d || ch >= 0x0859 && ch <= 0x085b || ch >= 0x08d3 && ch <= 0x08e1 || ch >= 0x08e3 && ch <= 0x0903 || ch >= 0x093a && ch <= 0x093c || ch >= 0x093e && ch <= 0x094f || ch >= 0x0951 && ch <= 0x0957 || ch == 0x0962 || ch == 0x0963 || ch >= 0x0966 && ch <= 0x096f || ch >= 0x0981 && ch <= 0x0983 || ch == 0x09bc || ch >= 0x09be && ch <= 0x09c4 || ch == 0x09c7 || ch == 0x09c8 || ch >= 0x09cb && ch <= 0x09cd || ch == 0x09d7 || ch == 0x09e2 || ch == 0x09e3 || ch >= 0x09e6 && ch <= 0x09ef || ch == 0x09fe || ch >= 0x0a01 && ch <= 0x0a03 || ch == 0x0a3c || ch >= 0x0a3e && ch <= 0x0a42 || ch == 0x0a47 || ch == 0x0a48 || ch >= 0x0a4b && ch <= 0x0a4d || ch == 0x0a51 || ch >= 0x0a66 && ch <= 0x0a71 || ch == 0x0a75 || ch >= 0x0a81 && ch <= 0x0a83 || ch == 0x0abc || ch >= 0x0abe && ch <= 0x0ac5 || ch >= 0x0ac7 && ch <= 0x0ac9 || ch >= 0x0acb && ch <= 0x0acd || ch == 0x0ae2 || ch == 0x0ae3 || ch >= 0x0ae6 && ch <= 0x0aef || ch >= 0x0afa && ch <= 0x0aff || ch >= 0x0b01 && ch <= 0x0b03 || ch == 0x0b3c || ch >= 0x0b3e && ch <= 0x0b44 || ch == 0x0b47 || ch == 0x0b48 || ch >= 0x0b4b && ch <= 0x0b4d || ch == 0x0b56 || ch == 0x0b57 || ch == 0x0b62 || ch == 0x0b63 || ch >= 0x0b66 && ch <= 0x0b6f || ch == 0x0b82 || ch >= 0x0bbe && ch <= 0x0bc2 || ch >= 0x0bc6 && ch <= 0x0bc8 || ch >= 0x0bca && ch <= 0x0bcd || ch == 0x0bd7 || ch >= 0x0be6 && ch <= 0x0bef || ch >= 0x0c00 && ch <= 0x0c04 || ch >= 0x0c3e && ch <= 0x0c44 || ch >= 0x0c46 && ch <= 0x0c48 || ch >= 0x0c4a && ch <= 0x0c4d || ch == 0x0c55 || ch == 0x0c56 || ch == 0x0c62 || ch == 0x0c63 || ch >= 0x0c66 && ch <= 0x0c6f || ch >= 0x0c81 && ch <= 0x0c83 || ch == 0x0cbc || ch >= 0x0cbe && ch <= 0x0cc4 || ch >= 0x0cc6 && ch <= 0x0cc8 || ch >= 0x0cca && ch <= 0x0ccd || ch == 0x0cd5 || ch == 0x0cd6 || ch == 0x0ce2 || ch == 0x0ce3 || ch >= 0x0ce6 && ch <= 0x0cef || ch >= 0x0d00 && ch <= 0x0d03 || ch == 0x0d3b || ch == 0x0d3c || ch >= 0x0d3e && ch <= 0x0d44 || ch >= 0x0d46 && ch <= 0x0d48 || ch >= 0x0d4a && ch <= 0x0d4d || ch == 0x0d57 || ch == 0x0d62 || ch == 0x0d63 || ch >= 0x0d66 && ch <= 0x0d6f || ch == 0x0d82 || ch == 0x0d83 || ch == 0x0dca || ch >= 0x0dcf && ch <= 0x0dd4 || ch == 0x0dd6 || ch >= 0x0dd8 && ch <= 0x0ddf || ch >= 0x0de6 && ch <= 0x0def || ch == 0x0df2 || ch == 0x0df3 || ch == 0x0e31 || ch >= 0x0e34 && ch <= 0x0e3a || ch >= 0x0e47 && ch <= 0x0e4e || ch >= 0x0e50 && ch <= 0x0e59 || ch == 0x0eb1 || ch >= 0x0eb4 && ch <= 0x0eb9 || ch == 0x0ebb || ch == 0x0ebc || ch >= 0x0ec8 && ch <= 0x0ecd || ch >= 0x0ed0 && ch <= 0x0ed9 || ch == 0x0f18 || ch == 0x0f19 || ch >= 0x0f20 && ch <= 0x0f29 || ch == 0x0f35 || ch == 0x0f37 || ch == 0x0f39 || ch == 0x0f3e || ch == 0x0f3f || ch >= 0x0f71 && ch <= 0x0f84 || ch == 0x0f86 || ch == 0x0f87 || ch >= 0x0f8d && ch <= 0x0f97 || ch >= 0x0f99 && ch <= 0x0fbc || ch == 0x0fc6 || ch >= 0x102b && ch <= 0x103e || ch >= 0x1040 && ch <= 0x1049 || ch >= 0x1056 && ch <= 0x1059 || ch >= 0x105e && ch <= 0x1060 || ch >= 0x1062 && ch <= 0x1064 || ch >= 0x1067 && ch <= 0x106d || ch >= 0x1071 && ch <= 0x1074 || ch >= 0x1082 && ch <= 0x108d || ch >= 0x108f && ch <= 0x109d || ch >= 0x135d && ch <= 0x135f || ch >= 0x1369 && ch <= 0x1371 || ch >= 0x1712 && ch <= 0x1714 || ch >= 0x1732 && ch <= 0x1734 || ch == 0x1752 || ch == 0x1753 || ch == 0x1772 || ch == 0x1773 || ch >= 0x17b4 && ch <= 0x17d3 || ch == 0x17dd || ch >= 0x17e0 && ch <= 0x17e9 || ch >= 0x180b && ch <= 0x180d || ch >= 0x1810 && ch <= 0x1819 || ch == 0x18a9 || ch >= 0x1920 && ch <= 0x192b || ch >= 0x1930 && ch <= 0x193b || ch >= 0x1946 && ch <= 0x194f || ch >= 0x19d0 && ch <= 0x19da || ch >= 0x1a17 && ch <= 0x1a1b || ch >= 0x1a55 && ch <= 0x1a5e || ch >= 0x1a60 && ch <= 0x1a7c || ch >= 0x1a7f && ch <= 0x1a89 || ch >= 0x1a90 && ch <= 0x1a99 || ch >= 0x1ab0 && ch <= 0x1abd || ch >= 0x1b00 && ch <= 0x1b04 || ch >= 0x1b34 && ch <= 0x1b44 || ch >= 0x1b50 && ch <= 0x1b59 || ch >= 0x1b6b && ch <= 0x1b73 || ch >= 0x1b80 && ch <= 0x1b82 || ch >= 0x1ba1 && ch <= 0x1bad || ch >= 0x1bb0 && ch <= 0x1bb9 || ch >= 0x1be6 && ch <= 0x1bf3 || ch >= 0x1c24 && ch <= 0x1c37 || ch >= 0x1c40 && ch <= 0x1c49 || ch >= 0x1c50 && ch <= 0x1c59 || ch >= 0x1cd0 && ch <= 0x1cd2 || ch >= 0x1cd4 && ch <= 0x1ce8 || ch == 0x1ced || ch >= 0x1cf2 && ch <= 0x1cf4 || ch >= 0x1cf7 && ch <= 0x1cf9 || ch >= 0x1dc0 && ch <= 0x1df9 || ch >= 0x1dfb && ch <= 0x1dff || ch == 0x203f || ch == 0x2040 || ch == 0x2054 || ch >= 0x20d0 && ch <= 0x20dc || ch == 0x20e1 || ch >= 0x20e5 && ch <= 0x20f0 || ch >= 0x2cef && ch <= 0x2cf1 || ch == 0x2d7f || ch >= 0x2de0 && ch <= 0x2dff || ch >= 0x302a && ch <= 0x302f || ch == 0x3099 || ch == 0x309a || ch >= 0xa620 && ch <= 0xa629 || ch == 0xa66f || ch >= 0xa674 && ch <= 0xa67d || ch == 0xa69e || ch == 0xa69f || ch == 0xa6f0 || ch == 0xa6f1 || ch == 0xa802 || ch == 0xa806 || ch == 0xa80b || ch >= 0xa823 && ch <= 0xa827 || ch == 0xa880 || ch == 0xa881 || ch >= 0xa8b4 && ch <= 0xa8c5 || ch >= 0xa8d0 && ch <= 0xa8d9 || ch >= 0xa8e0 && ch <= 0xa8f1 || ch >= 0xa8ff && ch <= 0xa909 || ch >= 0xa926 && ch <= 0xa92d || ch >= 0xa947 && ch <= 0xa953 || ch >= 0xa980 && ch <= 0xa983 || ch >= 0xa9b3 && ch <= 0xa9c0 || ch >= 0xa9d0 && ch <= 0xa9d9 || ch == 0xa9e5 || ch >= 0xa9f0 && ch <= 0xa9f9 || ch >= 0xaa29 && ch <= 0xaa36 || ch == 0xaa43 || ch == 0xaa4c || ch == 0xaa4d || ch >= 0xaa50 && ch <= 0xaa59 || ch >= 0xaa7b && ch <= 0xaa7d || ch == 0xaab0 || ch >= 0xaab2 && ch <= 0xaab4 || ch == 0xaab7 || ch == 0xaab8 || ch == 0xaabe || ch == 0xaabf || ch == 0xaac1 || ch >= 0xaaeb && ch <= 0xaaef || ch == 0xaaf5 || ch == 0xaaf6 || ch >= 0xabe3 && ch <= 0xabea || ch == 0xabec || ch == 0xabed || ch >= 0xabf0 && ch <= 0xabf9 || ch == 0xfb1e || ch >= 0xfe00 && ch <= 0xfe0f || ch >= 0xfe20 && ch <= 0xfe2f || ch == 0xfe33 || ch == 0xfe34 || ch >= 0xfe4d && ch <= 0xfe4f || ch >= 0xff10 && ch <= 0xff19 || ch == 0xff3f;
}

// These are a run-length and offset encoded representation of the
// >0xffff code points that are a valid part of identifiers. The
// offset starts at 0x10000, and each pair of numbers represents an
// offset to the next range, and then a size of the range. They were
// generated by bin/generate-identifier-regex.js

static uint32_t astralIdentifierStartCodes[] = { 0,11,2,25,2,18,2,1,2,14,3,13,35,122,70,52,268,28,4,48,48,31,14,29,6,37,11,29,3,35,5,7,2,4,43,157,19,35,5,35,5,39,9,51,157,310,10,21,11,7,153,5,3,0,2,43,2,1,4,0,3,22,11,22,10,30,66,18,2,1,11,21,11,25,71,55,7,1,65,0,16,3,2,2,2,28,43,28,4,28,36,7,2,27,28,53,11,21,11,18,14,17,111,72,56,50,14,50,14,35,349,41,7,1,79,28,11,0,9,21,107,20,28,22,13,52,76,44,33,24,27,35,30,0,3,0,9,34,4,0,13,47,15,3,22,0,2,0,36,17,2,24,85,6,2,0,2,3,2,14,2,9,8,46,39,7,3,1,3,21,2,6,2,1,2,4,4,0,19,0,13,4,159,52,19,3,21,2,31,47,21,1,2,0,185,46,42,3,37,47,21,0,60,42,14,0,72,26,230,43,117,63,32,7,3,0,3,7,2,1,2,23,16,0,2,0,95,7,3,38,17,0,2,0,29,0,11,39,8,0,22,0,12,45,20,0,35,56,264,8,2,36,18,0,50,29,113,6,2,1,2,37,22,0,26,5,2,1,2,31,15,0,328,18,190,0,80,921,103,110,18,195,2749,1070,4050,582,8634,568,8,30,114,29,19,47,17,3,32,20,6,18,689,63,129,74,6,0,67,12,65,1,2,0,29,6135,9,1237,43,8,8952,286,50,2,18,3,9,395,2309,106,6,12,4,8,8,9,5991,84,2,70,2,1,3,0,3,1,3,3,2,11,2,0,2,6,2,64,2,3,3,7,2,6,2,27,2,3,2,4,2,0,4,6,2,339,3,24,2,24,2,30,2,24,2,30,2,24,2,30,2,24,2,30,2,24,2,7,2357,44,11,6,17,0,370,43,1301,196,60,67,8,0,1205,3,2,26,2,1,2,0,3,0,2,9,2,3,2,0,2,0,7,0,5,0,2,0,2,0,2,2,2,1,2,0,3,0,2,0,2,0,2,0,2,0,2,1,2,0,3,3,2,6,2,3,2,3,2,0,2,9,2,16,6,2,2,4,2,16,4421,42717,35,4148,12,221,3,5761,15,7472,3104,541,1507,4938 };

static uint32_t astralIdentifierCodes[] = { 509,0,227,0,150,4,294,9,1368,2,2,1,6,3,41,2,5,0,166,1,574,3,9,9,370,1,154,10,176,2,54,14,32,9,16,3,46,10,54,9,7,2,37,13,2,9,6,1,45,0,13,2,49,13,9,3,2,11,83,11,7,0,161,11,6,9,7,3,56,1,2,6,3,1,3,2,10,0,11,1,3,6,4,4,193,17,10,9,5,0,82,19,13,9,214,6,3,8,28,1,83,16,16,9,82,12,9,9,84,14,5,9,243,14,166,9,71,5,2,1,3,3,2,0,2,1,13,9,120,6,3,6,4,0,29,9,41,6,2,3,9,0,10,10,47,15,406,7,2,7,17,9,57,21,2,13,123,5,4,0,2,1,2,6,2,0,9,9,49,4,2,1,2,4,9,9,330,3,19306,9,135,4,60,6,26,9,1014,0,2,54,8,3,82,0,12,1,19628,1,5319,4,4,5,9,7,3,6,31,3,149,2,1418,49,513,54,5,49,9,0,15,0,23,4,2,14,1361,6,2,16,3,6,2,1,2,4,262,6,10,9,419,13,1495,6,110,6,6,9,4759,9,787719,239 };

// This has a complexity linear to the value of the code. The
// assumption is that looking up astral identifier characters is
// rare.
bool isInAstralIdentifierStartCodes (uint32_t code) {
  uint32_t cur = 0x10000;
  for (unsigned int i = 0; i < sizeof(astralIdentifierStartCodes) / sizeof(uint32_t); i += 2) {
    cur += astralIdentifierStartCodes[i];
    if (cur > code)
      return false;
    cur += astralIdentifierStartCodes[i + 1];
    if (cur >= code)
      return true;
  }
  return false;
}

bool isInAstralIdentifierCodes (uint32_t code) {
  uint32_t cur = 0x10000;
  for (unsigned int i = 0; i < sizeof(astralIdentifierCodes) / sizeof(uint32_t); i += 2) {
    cur += astralIdentifierCodes[i];
    if (cur > code)
      return false;
    cur += astralIdentifierCodes[i + 1];
    if (cur >= code)
      return true;
  }
  return false;
}

// Test whether a given character code starts an identifier.
bool isIdentifierStart(uint32_t code) {
  if (code < 65) return code == 36;
  if (code < 91) return true;
  if (code < 97) return code == 95;
  if (code < 123) return true;
  if (code <= 0xffff) return code >= 0xaa && isNonASCIIidentifierStartChar(code);
  return isInAstralIdentifierStartCodes(code);
}

// Test whether a given character is part of an identifier.

bool isIdentifierChar(uint32_t code) {
  if (code < 48) return code == 36;
  if (code < 58) return true;
  if (code < 65) return false;
  if (code < 91) return true;
  if (code < 97) return code == 95;
  if (code < 123) return true;
  if (code <= 0xffff) return code >= 0xaa && isNonASCIIidentifierChar(code);
  return isInAstralIdentifierStartCodes(code) || isInAstralIdentifierCodes(code);
}

uint32_t nextChar(State *state) {
  uint32_t c;
  int e;
  state->pos = utf8_decode(state->pos, &c, &e);
  return c;
}

// https://github.com/skeeto/branchless-utf8/blob/master/utf8.h
/* Decode the next character, C, from BUF, reporting errors in E.
 *
 * Since this is a branchless decoder, four bytes will be read from the
 * buffer regardless of the actual length of the next character. This
 * means the buffer _must_ have at least three bytes of zero padding
 * following the end of the data stream.
 *
 * Errors are reported in E, which will be non-zero if the parsed
 * character was somehow invalid: invalid byte sequence, non-canonical
 * encoding, or a surrogate half.
 *
 * The function returns a pointer to the next character. When an error
 * occurs, this pointer will be a guess that depends on the particular
 * error, but it will always advance at least one byte.
 */
static void *utf8_decode(void *buf, uint32_t *c, int *e) {
  static const char lengths[] = {
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 3, 3, 4, 0
  };
  static const int masks[]  = {0x00, 0x7f, 0x1f, 0x0f, 0x07};
  static const uint32_t mins[] = {4194304, 0, 128, 2048, 65536};
  static const int shiftc[] = {0, 18, 12, 6, 0};
  static const int shifte[] = {0, 6, 4, 2, 0};

  unsigned char *s = buf;
  int len = lengths[s[0] >> 3];

  /* Compute the pointer to the next character early so that the next
    * iteration can start working on the next character. Neither Clang
    * nor GCC figure out this reordering on their own.
    */
  unsigned char *next = s + len + !len;

  /* Assume a four-byte character and load four bytes. Unused bits are
    * shifted out.
    */
  *c  = (uint32_t)(s[0] & masks[len]) << 18;
  *c |= (uint32_t)(s[1] & 0x3f) << 12;
  *c |= (uint32_t)(s[2] & 0x3f) <<  6;
  *c |= (uint32_t)(s[3] & 0x3f) <<  0;
  *c >>= shiftc[len];

  /* Accumulate the various error conditions. */
  *e  = (*c < mins[len]) << 6; // non-canonical encoding
  *e |= ((*c >> 11) == 0x1b) << 7;  // surrogate half?
  *e |= (*c > 0x10FFFF) << 8;  // out of range?
  *e |= (s[1] & 0xc0) >> 2;
  *e |= (s[2] & 0xc0) >> 4;
  *e |= (s[3]       ) >> 6;
  *e ^= 0x2a; // top two bits of each tail byte correct?
  *e >>= shifte[len];

  return next;
}

void bail (State *state, uint32_t error) {
  state->has_error = true;
  state->result->parse_error = error;
  state->pos = state->end + 1;
}

void syntaxError (State *state) {
  state->has_error = true;
  state->result->parse_error = state->pos - state->source;
  state->pos = state->end + 1;
}
