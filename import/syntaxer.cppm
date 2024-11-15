module;

#include <cassert>
#include <fstream>
#include <iostream>
#include <ranges>
#include <vector>
#include <span>
#include <set>

export module syntaxer;

import lexer;
import lexem;

#define OPTIMIZING_ASSERT(cond) \
  assert(cond);                 \
  [[assume(cond)]]

using namespace std::string_view_literals;
using namespace std::string_literals;

template <>
struct std::formatter<decltype(std::declval<Lexem>().GetPosition())> {
  constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  constexpr auto format(const auto& obj, std::format_context& ctx) const {
    return std::format_to(ctx.out(), "{}:{}", obj.line, obj.index);
  }
};


std::string_view ToString(Lex lex) {
  switch (lex) {
    case Lex::kId: {
      return "identifier"sv;
    }
    case Lex::kKeyworkd: {
      return "keyword"sv;
    }
    case Lex::kSeparator: {
      return "separator"sv;
    }
    case Lex::kOperator: {
      return "operator"sv;
    }
    case Lex::kEndLine: {
      return "endline"sv;
    }
    case Lex::kFloatLiter: {
      return "float"sv;
    }
    case Lex::kIntLiter: {
      return "int"sv;
    }
    case Lex::kStringLiter: {
      return "string"sv;
    }
  }
}

export class SyntaxValidator {
 public:
  SyntaxValidator(std::string filename) {
    auto lexes_own = Lexer(filename, "token.txt").Scan();
    bool prev_endline = false;
    std::vector<Lexem> lexes_filtered;
    for (auto lex : lexes_own) {
      if (lex.GetType() == Lex::kEndLine) {
        prev_endline = true;
        lexes_filtered.push_back(lex);
      } else {
        if (prev_endline || lex.GetType() != Lex::kSeparator) {
          if (lex.GetType() == Lex::kKeyworkd && (lex.GetData() == "not" || lex.GetData() == "and" || lex.GetData() == "or")) {
            lexes_filtered.emplace_back(Lex::kOperator, std::string(lex.GetData()), lex.GetPosition().line, lex.GetPosition().index);
          } else {
            lexes_filtered.push_back(lex);
          }
        }
        prev_endline = false;
      }
    }
    lexes_ = lexes_filtered;
    Program();
    if (!lexes_.empty()) {
      throw std::invalid_argument(std::format("Unexpected tabulation at {}", lexes_[0].GetPosition()));
    }
    std::println("All ok");
  }

 private:
  std::span<Lexem> lexes_;
  int in_cycle_ = 0;
  size_t cur_indent_ = 0;

  void Program() {
    while (!lexes_.empty()) {
      while (lexes_.at(0).GetType() == Lex::kEndLine) {
        SkipLexem(Lex::kEndLine);
      }
      if (SpacesAmount() != cur_indent_) {
        return;
      }
      if (lexes_.at(0).GetType() == Lex::kSeparator) {
        SkipLexem(Lex::kSeparator);
      }
      if (lexes_.at(0).GetType() == Lex::kEndLine) {
        SkipLexem(Lex::kEndLine);
      } else if (lexes_.at(0).GetType() == Lex::kKeyworkd) {
        if (lexes_.at(0).GetData() == "def") {
          DeclFunc();
        } else if (lexes_.at(0).GetData() == "if") {
          IfElse();
        } else if (lexes_.at(0).GetData() == "while") {
          While();
        } else if (lexes_.at(0).GetData() == "match") {
          MatchCase();
        } else if (lexes_.at(0).GetData() == "break") {
          if (in_cycle_ == 0) {
            throw std::runtime_error(std::format("SyntaxError: break out of cycle at {}", lexes_.at(0).GetData()));
          }
          SkipLexem(Lex::kKeyworkd, "break");
          SkipLexem(Lex::kEndLine);
        } else if (lexes_.at(0).GetData() == "pass") {
          SkipLexem(Lex::kKeyworkd, "pass");
          SkipLexem(Lex::kEndLine);
        } else if (lexes_.at(0).GetData() == "continue") {
          if (in_cycle_ == 0) {
            throw std::runtime_error(std::format("SyntaxError: break out of cycle at {}", lexes_[0].GetPosition()));
          }
          SkipLexem(Lex::kKeyworkd, "continue");
          SkipLexem(Lex::kEndLine);
        } else {
          throw std::invalid_argument(std::format("Am i stupid?: {}", lexes_.at(0).GetData()));
        }
      } else {
        Expression();
        SkipLexem(Lex::kEndLine);
      }
    }
  }

  void MatchCase() {
    SkipLexem(Lex::kKeyworkd, "match");
    Expression();
    SkipLexem(Lex::kKeyworkd, ":");
    cur_indent_ += 4;
    while (lexes_.at(0).GetType() == Lex::kKeyworkd && lexes_.at(0).GetData() == "case") {
      SkipLexem(Lex::kKeyworkd, "case");
      Expression();
      SkipLexem(Lex::kKeyworkd, ":");
      cur_indent_ += 4;
      Program();
      cur_indent_ -= 4;
      if (lexes_.at(0).GetType() == Lex::kSeparator &&
          lexes_.size() > 1 &&
          lexes_.at(1).GetType() == Lex::kKeyworkd &&
          lexes_.at(1).GetData() == "case" &&
          SpacesAmount() == cur_indent_) {
        SkipLexem(Lex::kSeparator);
      }
    }
    cur_indent_ -= 4;
    SkipLexem(Lex::kEndLine);
  }

  void Expression() {
    int cur_brace_balance = 0;
    bool prev_operator = true;

    static std::set bin_op = {
      "*"sv, "/"sv, "%"sv, "<"sv, ">"sv, "=="sv, "!="sv, "<="sv, ">="sv, "="sv, "**"sv, "//"sv, "."sv, ","sv, "or"sv, "and"sv
    };
    static std::set un_op = {
      "+"sv, "-"sv, "not"sv
    };

    while (lexes_.at(0).GetType() != Lex::kKeyworkd && lexes_.at(0).GetType() != Lex::kEndLine) {
      if (lexes_.at(0).GetType() == Lex::kOperator) {
        if (lexes_.at(0).GetData() == "(") {
          prev_operator = true;
          ++cur_brace_balance;
        } else if (lexes_.at(0).GetData() == ")") {
          if (prev_operator) {
            throw std::invalid_argument(std::format("SyntaxError: invalid expression at {}: closing brace after operator", lexes_.at(0).GetPosition()));
          }
          prev_operator = false;
          --cur_brace_balance;
          if (cur_brace_balance < 0) {
            throw std::invalid_argument(std::format("SyntaxError: extra brace at {}", lexes_.at(0).GetPosition()));
          }
        } else if (un_op.contains(lexes_.at(0).GetData())) {
          prev_operator = true;
        } else if (bin_op.contains(lexes_.at(0).GetData())) {
          if (prev_operator) {
            throw std::invalid_argument(std::format("SyntaxError: invalid expression at {}: two operators in a row", lexes_.at(0).GetPosition()));
          }
          prev_operator = true;
        } else {
          OPTIMIZING_ASSERT(false);
        }
      } else {
        if (!prev_operator) {
          throw std::invalid_argument(std::format("SyntaxError: invalid expression at {}: two expressions in a row", lexes_.at(0).GetPosition()));
        }
        prev_operator = false;
      }
      SkipLexem(lexes_.at(0).GetType());
    }
    if (cur_brace_balance > 0) {
      throw std::invalid_argument(std::format("SyntaxError: need extra brace at {}", lexes_.at(0).GetPosition()));
    }
  }

  void IfElse() {
    SkipLexem(Lex::kKeyworkd, "if");
    Expression();
    SkipLexem(Lex::kKeyworkd, ":");
    SkipLexem(Lex::kEndLine);
    cur_indent_ += 4;
    Program();
    cur_indent_ -= 4;
    if (!lexes_.empty() && lexes_.at(0).GetType() == Lex::kKeyworkd && lexes_.at(0).GetData() == "else") {
      SkipLexem(Lex::kKeyworkd, "else");
      SkipLexem(Lex::kKeyworkd, ":");
      cur_indent_ += 4;
      Program();
      cur_indent_ -= 4;
    }
  }

  void DeclFunc() {
    SkipLexem(Lex::kKeyworkd, "def");
    SkipLexem(Lex::kId);
    SkipLexem(Lex::kOperator, "(");
    while (lexes_.at(0).GetType() != Lex::kOperator ||
           lexes_.at(0).GetData() != ")") {
      SkipParam();
      SkipLexem(Lex::kOperator, ",");
    }
    SkipLexem(Lex::kOperator, ")");
    SkipLexem(Lex::kKeyworkd, ":");
    SkipLexem(Lex::kEndLine);
    cur_indent_ += 4;
    Program();
    cur_indent_ -= 4;
  }

  void While() {
    SkipLexem(Lex::kKeyworkd, "while");
    Expression();
    SkipLexem(Lex::kKeyworkd, ":");
    SkipLexem(Lex::kEndLine);
    cur_indent_ += 4;
    ++in_cycle_;
    Program();
    --in_cycle_;
    cur_indent_ -= 4;
  }

  void SkipParam() {
    SkipLexem(Lex::kId);
    SkipLexem(Lex::kKeyworkd, ":");
    SkipLexem(Lex::kId);
  }

  void SkipLexem(Lex type) {
    OPTIMIZING_ASSERT(type != Lex::kKeyworkd);
    if (lexes_.at(0).GetType() != type) {
      throw std::invalid_argument(std::format(
          "expected {}, got {} at {}", ToString(type), lexes_[0].GetData(), lexes_[0].GetPosition()));
    }
    lexes_ = lexes_.subspan(1);
  }

  void SkipLexem(Lex type, std::string_view data) {
    if (lexes_.at(0).GetType() != type || lexes_.at(0).GetData() != data) {
      throw std::invalid_argument(std::format(
          "expected {}, got {} at {}", ToString(type), lexes_[0].GetData(), lexes_[0].GetPosition()));
    }
    lexes_ = lexes_.subspan(1);
  }

  size_t SpacesAmount() {
    return lexes_.at(0).GetType() == Lex::kSeparator ? lexes_.at(0).GetData().size() : 0;
  }
};
