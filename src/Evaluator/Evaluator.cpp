#include "Builtin.h"
#include "Evaluator.h"
#include "Error.h"

#define CAST(T) auto x = ASTCast<AST::T>(ast)

namespace fire::eval {

ObjPtr<ObjNone> Evaluator::_None;

Evaluator::Evaluator() {
  _None = ObjNew<ObjNone>();
}

Evaluator::~Evaluator() {
}

Evaluator::VarStackPtr Evaluator::push_stack(size_t var_count) {
  return this->var_stack.emplace_front(std::make_shared<VarStack>(var_count));
}

void Evaluator::pop_stack() {
  this->var_stack.pop_front();
}

Evaluator::VarStack& Evaluator::get_cur_stack() {
  return **this->var_stack.begin();
}

Evaluator::VarStack& Evaluator::get_stack(int distance) {
  auto it = this->var_stack.begin();

  for (int i = 0; i < distance; i++)
    it++;

  return **it;
}

ObjPointer& Evaluator::eval_as_left(ASTPointer ast) {

  switch (ast->kind) {
  case ASTKind::IndexRef: {
    auto ex = ast->as_expr();

    return this->eval_index_ref(this->eval_as_left(ex->lhs), this->evaluate(ex->rhs));
  }
  }

  assert(ast->kind == ASTKind::Variable);

  auto x = ast->GetID();

  return this->get_stack(x->depth /*distance*/).var_list[x->index];
}

ObjPointer& Evaluator::eval_index_ref(ObjPointer array, ObjPointer _index_obj) {
  assert(_index_obj->type.kind == TypeKind::Int);

  i64 index = _index_obj->As<ObjPrimitive>()->vi;

  switch (array->type.kind) {
  case TypeKind::Dict: {
    todo_impl;
  }
  }

  assert(array->type.kind == TypeKind::Vector);

  return array->As<ObjIterable>()->list[(size_t)index];
}

ObjPointer Evaluator::evaluate(ASTPointer ast) {
  using Kind = ASTKind;

  if (!ast) {
    return _None;
  }

  switch (ast->kind) {

  case Kind::Function:
  case Kind::Class:
  case Kind::Enum:
    break;

  case Kind::Identifier:
  case Kind::ScopeResol:
  case Kind::MemberAccess:
    // この２つの Kind は、意味解析で変更されているはず。
    // ここまで来ている場合、バグです。
    debug(Error(ast, "??").emit());
    panic;

  case Kind::Value: {
    return ast->as_value()->value;
  }

  case Kind::Variable: {
    auto x = ast->GetID();

    return this->get_stack(x->depth /*distance*/).var_list[x->index];
  }

  case Kind::Array: {
    CAST(Array);

    auto obj = ObjNew<ObjIterable>(TypeInfo(TypeKind::Vector, {x->elem_type}));

    for (auto&& e : x->elements)
      obj->Append(this->evaluate(e));

    return obj;
  }

  case Kind::IndexRef: {
    auto ex = ast->as_expr();

    return this->eval_index_ref(this->evaluate(ex->lhs), this->evaluate(ex->rhs));
  }

  case Kind::FuncName: {
    auto id = ast->GetID();
    auto obj = ObjNew<ObjCallable>(id->candidates[0]);

    obj->type.params = id->ft_args;
    obj->type.params.insert(obj->type.params.begin(), id->ft_ret);

    return obj;
  }

  case Kind::BuiltinFuncName: {
    auto id = ast->GetID();
    auto obj = ObjNew<ObjCallable>(id->candidates_builtin[0]);

    obj->type.params = id->ft_args;
    obj->type.params.insert(obj->type.params.begin(), id->ft_ret);

    return obj;
  }

  case Kind::Enumerator: {
    return ObjNew<ObjEnumerator>(ast->GetID()->ast_enum, ast->GetID()->index);
  }

  case Kind::EnumName: {
    return ObjNew<ObjType>(ast->GetID()->ast_enum);
  }

  case Kind::ClassName: {
    return ObjNew<ObjType>(ast->GetID()->ast_class);
  }

  case Kind::MemberVariable: {
    auto ex = ast->as_expr();

    auto inst = PtrCast<ObjInstance>(this->evaluate(ex->lhs));

    auto id = ASTCast<AST::Identifier>(ex->rhs);

    return inst->get_mvar(id->index);
  }

  case Kind::MemberFunction: {
    return ObjNew<ObjCallable>(ast->GetID()->candidates[0]);
  }

  case Kind::BuiltinMemberVariable: {
    auto self = ast->as_expr()->lhs;
    auto id = ast->GetID();

    return id->blt_member_var->impl(self, this->evaluate(self));
  }

  case Kind::BuiltinMemberFunction: {
    auto expr = ast->as_expr();
    auto id = expr->GetID();

    auto callable = ObjNew<ObjCallable>(id->candidates_builtin[0]);

    callable->selfobj = this->evaluate(expr->lhs);
    callable->is_member_call = true;

    return callable;
  }

  case Kind::CallFunc: {
    CAST(CallFunc);

    ObjVector args;

    for (auto&& arg : x->args) {
      args.emplace_back(this->evaluate(arg));
    }

    auto _func = x->callee_ast;
    auto _builtin = x->callee_builtin;

    if (x->call_functor) {
      auto functor = PtrCast<ObjCallable>(this->evaluate(x->callee));

      if (functor->func)
        _func = functor->func;
      else
        _builtin = functor->builtin;
    }

    if (_builtin) {
      return _builtin->Call(x, std::move(args));
    }

    auto stack = this->push_stack(x->args.size());

    if (this->var_stack.size() >= 1588) {
      throw Error(ast->token, "stack overflow");
    }

    this->call_stack.push_front(stack);

    stack->var_list = std::move(args);

    this->evaluate(_func->block);

    auto result = stack->func_result;

    this->pop_stack();
    this->call_stack.pop_front();

    return result ? result : _None;
  }

  case Kind::CallFunc_Ctor: {
    CAST(CallFunc);

    auto ast_class = x->get_class_ptr();

    auto inst = ObjNew<ObjInstance>(ast_class);

    size_t const argc = x->args.size();

    inst->member_variables.reserve(argc);

    for (size_t i = 0; i < argc; i++) {
      if (auto init = ast_class->member_variables[i]->init; init) {
        inst->member_variables[i] = this->evaluate(init);
      }

      inst->member_variables[i] = this->evaluate(x->args[i]);
    }

    return inst;
  }

  case Kind::Assign: {
    auto x = ast->as_expr();

    return this->eval_as_left(x->lhs) = this->evaluate(x->rhs);
  }

  case Kind::Return:
  case Kind::Throw:
  case Kind::Break:
  case Kind::Continue:
  case Kind::Block:
  case Kind::If:
  case Kind::While:
  case Kind::TryCatch:
  case Kind::Vardef:
    this->eval_stmt(ast);
    break;

  default:
    if (ast->is_expr)
      return this->eval_expr(ASTCast<AST::Expr>(ast));

    alertexpr(static_cast<int>(ast->kind));
    todo_impl;
  }

  return _None;
}

} // namespace fire::eval