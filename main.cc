#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <clang/Rewrite/Frontend/Rewriters.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/CommandLine.h>
#include <set>
#include <sstream>
#include <stdlib.h>

#include "shell.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;
using namespace llvm;
using namespace shell;

class EMIInstrum : public RecursiveASTVisitor<EMIInstrum> {
  static constexpr const char *function_str =
      "#include <stdio.h>\n"
      "void __emi_report_block(unsigned blockid) {\n"
      "  static unsigned char reported_blocks[10000];\n"
      "  if (blockid < 10000) {\n"
      "    if (!reported_blocks[blockid]) {\n"
      "      reported_blocks[blockid] = 1;\n"
      "      printf(\"block:%d\\n\", blockid);\n"
      "    }\n"
      "  } else\n"
      "    printf(\"block:%d\\n\", blockid);\n"
      "}\n";

  ASTContext &Context;
  Rewriter TheRewriter;

public:
  unsigned blockid;

  static std::set<unsigned> findAllBlocks(
      const std::string &str) {
    std::set<unsigned> blockNumbers;

    for (size_t i = 0; i < str.size(); ++i) {
      if (str.substr(i, 6) == "block:") {
        int j = i + 6;
        unsigned num = 0;
        while (j < str.size() && std::isdigit(str[j])) {
          num = num * 10 + (str[j] - '0');
          ++j;
        }

        blockNumbers.insert(num);
        i = j;
      }
    }
    return blockNumbers;
  }

public:
  EMIInstrum(ASTContext &Context)
      : Context(Context), blockid(0) {
    TheRewriter.setSourceMgr(
        Context.getSourceManager(), Context.getLangOpts());
  }

  bool write(std::string ofilename) {
    std::error_code error_code;
    llvm::raw_fd_ostream outFile(
        ofilename, error_code, llvm::sys::fs::F_None);
    outFile << function_str;
    if (!error_code) {
      FileID id =
          Context.getSourceManager().getMainFileID();
      TheRewriter.getEditBuffer(id).write(outFile);
      outFile.close();
      return true;
    }
    return false;
  }

  bool VisitCompoundStmt(CompoundStmt *s) {
    std::string report_str = "__emi_report_block(" +
                             std::to_string(blockid) + ");";
    auto loc = s->getBeginLoc().getLocWithOffset(1);
    auto &SM = Context.getSourceManager();
    if (!loc.isMacroID() && SM.isInMainFile(loc) &&
        !s->body_empty()) {
      TheRewriter.InsertText(loc, report_str, true, true);
      blockid += 1;
    }
    return true;
  }
};

class EMIMutator : public RecursiveASTVisitor<EMIMutator> {
  ASTContext &Context;
  Rewriter TheRewriter;
  bool parent_is_removed = false;
  unsigned blockid = 0;
  unsigned targetBlock;

public:
  EMIMutator(ASTContext &Context, unsigned targetBlock)
      : Context(Context), targetBlock(targetBlock) {
    TheRewriter.setSourceMgr(
        Context.getSourceManager(), Context.getLangOpts());
  }

  bool write(std::string ofilename) {
    std::error_code error_code;
    llvm::raw_fd_ostream outFile(
        ofilename, error_code, llvm::sys::fs::F_None);
    if (!error_code) {
      FileID id =
          Context.getSourceManager().getMainFileID();
      TheRewriter.getEditBuffer(id).write(outFile);
      outFile.close();
      return true;
    }
    return false;
  }

  bool VisitCompoundStmt(CompoundStmt *s) {
    auto loc = s->getBeginLoc();
    auto &SM = Context.getSourceManager();
    if (!loc.isMacroID() && SM.isInMainFile(loc) &&
        !s->body_empty()) {
      if (blockid == targetBlock) {
        llvm::errs() << "try to remove " << blockid << "\n";
        TheRewriter.ReplaceText(s->getSourceRange(), "{}");
      }
      blockid += 1;
    }
    return true;
  }
};

static llvm::cl::OptionCategory MyToolCategory(
    "my-tool options");
static llvm::cl::opt<std::string> InputFilename(
    cl::Positional, cl::desc("<input source file>"),
    cl::init("-"), cl::value_desc("filename"),
    cl::cat(MyToolCategory));
static llvm::cl::opt<std::string> OutputFilename("o",
    cl::desc("<out source file>"), cl::init("out.c"),
    cl::value_desc("filename"), cl::cat(MyToolCategory));
static llvm::cl::opt<std::string> SaveInstrumFile(
    "save-instrum-file", cl::desc("instrumented file"),
    cl::cat(MyToolCategory));
static llvm::cl::opt<bool> QueryMutable("check-if-mutable",
    cl::desc("check if mutable"),
    cl::value_desc("check if mutable"),
    cl::cat(MyToolCategory));
static llvm::cl::opt<unsigned> BlockToRemove("remove",
    cl::init(-1u), cl::desc("remove target blocks"),
    cl::cat(MyToolCategory));
static llvm::cl::extrahelp CommonHelp(
    CommonOptionsParser::HelpMessage);
static llvm::cl::extrahelp MoreHelp(
    "\nMore help text...\n");

int main(int argc, const char **argv) {
  llvm::cl::HideUnrelatedOptions(MyToolCategory);
  llvm::cl::ParseCommandLineOptions(
      argc, argv, "driver with libTooling\n");

  std::vector<std::unique_ptr<ASTUnit>> units;
  clang::tooling::FixedCompilationDatabase compilations{
      ".", std::vector<std::string>{"-w"}};
  std::vector<std::string> srcList{InputFilename};
  clang::tooling::ClangTool Tool(compilations, srcList);
  Tool.buildASTs(units);

  assert(units[0]);
  ASTContext &Context = units[0]->getASTContext();
  EMIInstrum instrum(Context);
  instrum.TraverseAST(Context);

  std::string outSrcFile = OutputFilename;
  if (SaveInstrumFile.getNumOccurrences())
    outSrcFile = SaveInstrumFile;
  instrum.write(outSrcFile);

  std::set<unsigned> deadBlocks;
  ExecArgs args{.take_outs = true};
  std::string outExecFile = outSrcFile + ".elf";
  ExecResult res = ShellProcess::execute(
      "gcc -o " + outExecFile + " " + outSrcFile, args);
  if (res.return_code == 0) {
    res = ShellProcess::execute("./" + outExecFile, args);
    std::set<unsigned> reportedBlocks =
        EMIInstrum::findAllBlocks(res.outs);
    for (int i = 0; i < instrum.blockid; ++i) {
      if (reportedBlocks.find(i) == reportedBlocks.end())
        deadBlocks.insert(i);
    }
  }

  ShellProcess::execute("rm -rf " + outExecFile);

  llvm::errs() << "dead blocks:";
  for (unsigned blk : deadBlocks)
    llvm::errs() << " " << blk;
  llvm::errs() << "\n";
  if (QueryMutable) return deadBlocks.size() == 0;

  EMIMutator mutator(Context, BlockToRemove);
  mutator.TraverseAST(Context);
  mutator.write(OutputFilename);
  return 0;
}
