#include "AST.h"
#include "Evaluator.h"
#include "Error.h"
#include "Builtin.h"
#include "alert.h"

namespace fire::eval {

using namespace AST;

ObjPointer* Evaluator::find_var(std::string const& name) {
  for (auto it = this->stack.rbegin(); it != this->stack.rend(); it++) {
    if (auto pvar = it->find_variable(name); pvar)
      return &pvar->value;
  }

  return nullptr;
}

Evaluator::FunctionFindResult Evaluator::find_func(std::string const& name,
                                                   ASTPtr<AST::CallFunc> call) {
  FunctionFindResult result;

  (void)call;

  for (auto&& ast : this->functions) {
    if (ast->GetName() == name) {
      result.append(ast);
    }
  }

  for (auto&& b : builtins::get_builtin_functions()) {
    if (b.name == name)
      result.append(&b);
  }

  return result;
}

std::pair<ASTPtr<AST::Class>, ASTPtr<AST::Enum>>
Evaluator::find_class_or_enum(std::string const& name) {
  auto e = this->find_enum(name);

  for (auto&& c : this->classes) {
    if (c->GetName() == name)
      return {c, e};
  }

  return {nullptr, e};
}

ASTPtr<AST::Enum> Evaluator::find_enum(std::string const& name) {
  for (auto&& e : this->enums)
    if (e->GetName() == name)
      return e;

  return nullptr;
}

ObjPtr<ObjInstance> Evaluator::new_class_instance(ASTPtr<AST::Class> ast) {
  auto obj = ObjNew<ObjInstance>(ast);

  for (auto&& var : ast->get_member_variables()) {
    auto& v = obj->member[var->GetName()];

    if (var->init) {
      v = this->evaluate(var->init);
    }
  }

  for (auto&& func : ast->get_member_functions()) {
    auto objcb = obj->add_member_func(ObjNew<ObjCallable>(func));

    objcb->selfobj = obj;
    objcb->is_member_call = true;
  }

  return obj;
}

ObjPointer Evaluator::call_function_ast(bool have_self, ASTPtr<AST::Function> ast,
                                        ASTPtr<AST::CallFunc> call, ObjVector& args) {

  if (this->stack.size() >= EVALUATOR_STACK_MAX_SIZE)
    Error(call->token, "stack over flow.")();

  auto& stack = this->PushStack();

  std::map<std::string, bool> assignmented;

  auto formal = ast->arguments.begin();
  auto act = call->args.begin();

  if (have_self) {
    assignmented["self"] = true;
    stack.append("self", args[0]);
    formal++;
  }

  for (auto itobj = args.begin() + (int)have_self; act != call->args.end(); act++) {
    if (formal == ast->arguments.end() && !ast->is_var_arg) {
      Error(call->token, "too many arguments")();
    }

    std::string name;
    ASTPtr<AST::Argument> target = nullptr;

    if ((*act)->kind == ASTKind::SpecifyArgumentName) {
      auto expr = (*act)->As<AST::Expr>();

      name = expr->lhs->token.str;

      if (!(target = ast->find_arg(name))) {
        Error(expr->lhs, "'" + name + "' is not found in arguments of function '" +
                             ast->GetName() + "'")();
      }
    }
    else {
      target = *formal;
      name = (*formal++)->GetName();
    }

    if (assignmented[name]) {
      Error(*act, "set to same argument name again.")();
    }

    if (target->type) {
      auto formaltype = this->evaluate(target->type)->As<ObjType>()->typeinfo;

      if (!(*itobj)->type.equals(formaltype)) {
        Error((*act)->kind == ASTKind::SpecifyArgumentName ? (*act)->As<Expr>()->rhs
                                                           : (*act),
              "expected '" + formaltype.to_string() + "', but found '" +
                  (*itobj)->type.to_string() + "'")
            .emit();

        Error(target->type, "specified type here").emit(Error::ErrorLevel::Note).stop();
      }
    }

    stack.append(name, *itobj++);
    assignmented[name] = true;
  }

  for (auto&& arg : ast->arguments) {
    if (!assignmented[arg->GetName()]) {
      Error(call->token, "argument '" + arg->GetName() + "' was not assignment")();
    }
  }

  this->evaluate(ast->block);

  auto ret = stack.result;

  this->PopStack();

  return ret ? ret : ObjNew<ObjNone>();
}

Evaluator::LoopContext& Evaluator::EnterLoopStatement(ASTPtr<AST::Statement> ast) {
  return this->loop_stack.emplace_back(ast);
}

void Evaluator::LeaveLoop() {
  this->loop_stack.pop_back();
}

Evaluator::LoopContext& Evaluator::GetCurrentLoop() {
  return *this->loop_stack.rbegin();
}

void Evaluator::adjust_numeric_type_object(ObjPtr<ObjPrimitive> left,
                                           ObjPtr<ObjPrimitive> right) {

  auto lk = left->type.kind, rk = right->type.kind;

  if (lk == TypeKind::Char)
    left->type = TypeKind::Int;

  if (rk == TypeKind::Char)
    right->type = TypeKind::Int;

  if (lk == rk)
    return;

  if (left->is_float() != right->is_float()) {
    (left->is_float() ? left : right)->to_float();
    return;
  }

  if (lk == TypeKind::Size)
    right->vsize = (value_type::Size)right->vi;
  else // rk == TypeKind::Size
    left->vsize = (value_type::Size)left->vi;
}

} // namespace fire::eval