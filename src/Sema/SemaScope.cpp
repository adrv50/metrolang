#include <list>

#include "alert.h"
#include "Error.h"
#include "Sema/Sema.h"

namespace fire::semantics_checker {

ScopeContext::LocalVar::LocalVar(ASTPtr<AST::VarDef> vardef) {

  this->name = vardef->GetName();
  this->decl = vardef;
}

ScopeContext::LocalVar::LocalVar(ASTPtr<AST::Argument> arg) {
  // Don't use this ctor if arg is template.

  this->name = arg->GetName();
  this->arg = arg;
}

// ------------------------------------
//  ScopeContext ( base-struct )

bool ScopeContext::Contains(ScopeContext* scope, bool recursive) const {

  switch (this->type) {
  case SC_Block: {
    auto block = (BlockScope*)scope;

    for (auto&& c : block->child_scopes) {
      if (c == scope)
        return true;

      if (recursive && c->Contains(scope, recursive))
        return true;
    }

    break;
  }

  case SC_Func: {
    auto func = (FunctionScope*)scope;

    return func->block == scope || (recursive && func->block->Contains(scope, recursive));
  }

  default:
    todo_impl;
  }

  return false;
}

ASTPointer ScopeContext::GetAST() const {
  alert;
  return nullptr;
}

ScopeContext::LocalVar* ScopeContext::find_var(string const&) {
  alert;
  return nullptr;
}

ScopeContext* ScopeContext::find_child_scope(ASTPointer) {
  alert;
  return nullptr;
}

ScopeContext* ScopeContext::find_child_scope(ScopeContext*) {
  alert;
  return nullptr;
}

vector<ScopeContext*> ScopeContext::find_name(string const&) {
  alert;
  return {};
}

// ------------------------------------
//  BlockScope

BlockScope::BlockScope(int depth, ASTPtr<AST::Block> ast)
    : ScopeContext(SC_Block),
      ast(ast) {

  this->depth = depth;

  if (!ast)
    return;

  for (auto&& e : ast->list) {
    switch (e->kind) {
    case ASTKind::Block: {
      this->AddScope(new BlockScope(this->depth + 1, ASTCast<AST::Block>(e)));
      break;
    }

    case ASTKind::Function:
      this->AddScope(new FunctionScope(this->depth + 1, ASTCast<AST::Function>(e)));
      break;

    case ASTKind::Vardef:
      this->add_var(ASTCast<AST::VarDef>(e));
      break;

    case ASTKind::If: {
      auto d = e->As<AST::Statement>()->get_data<AST::Statement::If>();

      this->AddScope(new BlockScope(this->depth + 1,
                                    ASTCast<AST::Block>(ASTCast<AST::Block>(d.if_true))));

      if (d.if_false) {
        this->AddScope(new BlockScope(
            this->depth + 1, ASTCast<AST::Block>(ASTCast<AST::Block>(d.if_false))));
      }

      break;
    }

    case ASTKind::While: {
      this->AddScope(new BlockScope(
          this->depth + 1, e->as_stmt()->get_data<AST::Statement::While>().block));

      break;
    }

    case ASTKind::Switch:
      todo_impl;

    case ASTKind::TryCatch: {
      auto d = e->as_stmt()->get_data<AST::Statement::TryCatch>();

      this->AddScope(new BlockScope(this->depth + 1, d.tryblock));

      for (auto&& c : d.catchers) {
        auto b = new BlockScope(this->depth + 1, c.catched);

        auto& lvar = b->variables.emplace_back();

        lvar.name = c.varname.str;
        lvar.depth = b->depth;

        this->AddScope(b);
      }

      break;
    }

    case ASTKind::Class: {
      auto x = ASTCast<AST::Class>(e);

      // Sema::GetInstance()->add_class(x);

      for (auto&& mf : x->member_functions)
        this->AddScope(new FunctionScope(this->depth + 1, mf));

      break;
    }

      // case ASTKind::Enum: {
      //   Sema::GetInstance()->add_enum(ASTCast<AST::Enum>(e));
      //   break;
      // }

    case ASTKind::Namespace: {
      auto block = ASTCast<AST::Block>(e);

      auto scope = new NamespaceScope(this->depth, block);

      // for (auto&& s : this->child_scopes) {
      //   if (auto ns = (NamespaceScope*)s;
      //       ns->type == SC_Namespace && ns->name == scope->name) {

      //     ns->_ast.emplace_back(block);

      //     for (auto&& v : scope->variables) {
      //       if (ns->find_var(v.name))
      //         continue;

      //       v.index += ns->variables.size();

      //       ns->variables.emplace_back(v);
      //     }

      //     for (auto&& s2 : scope->child_scopes) {
      //       ns->AddScope(s2);
      //       s2 = nullptr;
      //     }

      //     ns->child_var_count += scope->child_var_count;

      //     delete scope;
      //     goto __found;
      //   }
      // }

      auto c = (NamespaceScope*)this->AddScope(scope);

      this->child_var_count += c->child_var_count;

    __found:;
      break;
    }
    }
  }

  // alertexpr(var_count_total);
}

BlockScope::~BlockScope() {
  for (auto&& c : this->child_scopes)
    delete c;
}

ScopeContext*& BlockScope::AddScope(ScopeContext* scope) {

  if (scope->type == SC_Namespace) {

    auto ns = (NamespaceScope*)scope;

    for (auto&& c : this->child_scopes) {
      if (c->type != SC_Namespace)
        continue;

      auto nc = (NamespaceScope*)c;

      if (nc->name != ns->name)
        continue;

      nc->_ast.emplace_back(ns->ast);

      auto index_add = nc->child_var_count + this->child_var_count;

      for (auto&& v : ns->variables) {
        if (nc->find_var(v.name))
          continue;

        v.index_add = index_add;

        nc->variables.emplace_back(v);
      }

      nc->child_var_count += ns->child_var_count;

      for (auto&& x : ns->child_scopes) {
        nc->AddScope(x);
      }

      ns->child_scopes.clear();

      delete ns;

      return c;
    }
  }

_pass:
  scope->_owner = this;

  return this->child_scopes.emplace_back(scope);
}

ScopeContext::LocalVar& BlockScope::add_var(ASTPtr<AST::VarDef> def) {
  LocalVar* pvar = this->find_var(def->GetName());

  if (!pvar) {
    pvar = &this->variables.emplace_back(def);

    pvar->index = this->variables.size() - 1;
    pvar->index_add = this->child_var_count;
  }

  pvar->depth = this->depth;
  def->index = pvar->index;

  // pvar->index = def->index = this->variables.size() - 1;

  this->ast->stack_size++;

  // this->child_var_count++;

  return *pvar;
}

ASTPointer BlockScope::GetAST() const {
  return this->ast;
}

ScopeContext::LocalVar* BlockScope::find_var(string const& name) {
  for (auto&& var : this->variables) {
    if (var.name == name)
      return &var;
  }

  return nullptr;
}

ScopeContext* BlockScope::find_child_scope(ASTPointer ast) {
  if (this->ast == ast)
    return this;

  for (auto&& c : this->child_scopes) {
    if (auto s = c->find_child_scope(ast); s)
      return s;
  }

  return nullptr;
}

ScopeContext* BlockScope::find_child_scope(ScopeContext* ctx) {
  if (this == ctx)
    return this;

  for (auto&& c : this->child_scopes) {
    if (c == ctx)
      return c;
  }

  return nullptr;
}

vector<ScopeContext*> BlockScope::find_name(string const& name) {
  vector<ScopeContext*> vec;

  for (auto&& c : this->child_scopes) {
    if (c->IsNamedAs(name)) {
      vec.emplace_back(c);
    }
  }

  return vec;
}

std::string BlockScope::to_string() const {
  static int indent = 0;

  string s = "block depth=" + std::to_string(this->depth) + " {\n";

  int _indent = indent;
  indent++;

  for (auto&& x : this->child_scopes) {
    s += std::string(indent * 2, ' ') + x->to_string() + "\n";
  }

  indent = _indent;
  return s + "\n" + std::string(indent * 2, ' ') + "}";
}

// ------------------------------------
//  FunctionScope

FunctionScope::FunctionScope(int depth, ASTPtr<AST::Function> ast)
    : ScopeContext(SC_Func),
      ast(ast) {

  this->depth = depth;

  auto S = Sema::GetInstance();

  for (auto&& [_key, _val] : S->function_scope_map) {
    if (_key == ast) {
      todo_impl; // why again?
    }
  }

  // auto f = Sema::SemaFunction(ast);
  // f.scope = this;

  S->function_scope_map.emplace_back(ast, this);

  for (auto&& arg : ast->arguments) {
    this->add_arg(arg);
  }

  this->block = new BlockScope(this->depth + 1, ast->block);

  this->block->_owner = this;
}

FunctionScope::~FunctionScope() {
  delete this->block;
}

ScopeContext::LocalVar& FunctionScope::add_arg(ASTPtr<AST::Argument> def) {
  auto& arg = this->arguments.emplace_back(def->GetName());

  arg.arg = def;
  arg.is_argument = true;

  arg.depth = this->depth;
  arg.index = this->arguments.size() - 1;

  return arg;
}

ASTPointer FunctionScope::GetAST() const {
  return this->ast;
}

ScopeContext::LocalVar* FunctionScope::find_var(string const& name) {
  for (auto&& arg : this->arguments) {
    if (arg.name == name)
      return &arg;
  }

  return nullptr;
}

ScopeContext* FunctionScope::find_child_scope(ASTPointer ast) {
  if (this->ast == ast)
    return this;

  return this->block->find_child_scope(ast);
}

ScopeContext* FunctionScope::find_child_scope(ScopeContext* ctx) {
  if (this == ctx)
    return this;

  return this->block->find_child_scope(ctx);
}

vector<ScopeContext*> FunctionScope::find_name(string const& name) {
  return this->block->find_name(name);
}

std::string FunctionScope::to_string() const {
  return "function depth=" + std::to_string(this->depth) + " {\n" +
         this->block->to_string() + "\n}";
}

// ------------------------------------
//  NamespaceScope

NamespaceScope::NamespaceScope(int depth, ASTPtr<AST::Block> ast)
    : BlockScope(depth, ast),
      name(ast->token.str) {
  this->type = SC_Namespace;

  this->child_var_count = this->variables.size();
}

NamespaceScope::~NamespaceScope() {
}

// ------------------------------------

} // namespace fire::semantics_checker