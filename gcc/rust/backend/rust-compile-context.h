// Copyright (C) 2020 Free Software Foundation, Inc.

// This file is part of GCC.

// GCC is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3, or (at your option) any later
// version.

// GCC is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
// for more details.

// You should have received a copy of the GNU General Public License
// along with GCC; see the file COPYING3.  If not see
// <http://www.gnu.org/licenses/>.

#ifndef RUST_COMPILE_CONTEXT
#define RUST_COMPILE_CONTEXT

#include "rust-system.h"
#include "rust-hir-map.h"
#include "rust-name-resolver.h"
#include "rust-hir-type-check.h"
#include "rust-backend.h"
#include "rust-compile-tyty.h"
#include "rust-ast-full.h"

namespace Rust {
namespace Compile {

struct fncontext
{
  ::Bfunction *fndecl;
  ::Bvariable *ret_addr;
};

class Context
{
public:
  Context (::Backend *backend)
    : backend (backend), resolver (Resolver::Resolver::get ()),
      tyctx (Resolver::TypeCheckContext::get ()),
      mappings (Analysis::Mappings::get ())
  {
    // insert the builtins
    auto builtins = resolver->get_builtin_types ();
    for (auto it = builtins.begin (); it != builtins.end (); it++)
      {
	HirId ref;
	rust_assert (
	  tyctx->lookup_type_by_node_id ((*it)->get_node_id (), &ref));

	TyTy::TyBase *lookup;
	rust_assert (tyctx->lookup_type (ref, &lookup));

	auto compiled = TyTyCompile::compile (backend, lookup);
	compiled_type_map[ref] = compiled;
      }
  }

  ~Context () {}

  bool lookup_compiled_types (HirId id, ::Btype **type)
  {
    auto it = compiled_type_map.find (id);
    if (it == compiled_type_map.end ())
      return false;

    *type = it->second;
    return true;
  }

  void insert_compiled_type (HirId id, ::Btype *type)
  {
    compiled_type_map[id] = type;
  }

  ::Backend *get_backend () { return backend; }
  Resolver::Resolver *get_resolver () { return resolver; }
  Resolver::TypeCheckContext *get_tyctx () { return tyctx; }
  Analysis::Mappings *get_mappings () { return mappings; }

  void push_block (Bblock *scope)
  {
    scope_stack.push_back (scope);
    statements.push_back ({});
  }

  Bblock *pop_block ()
  {
    auto block = scope_stack.back ();
    scope_stack.pop_back ();

    auto stmts = statements.back ();
    statements.pop_back ();

    backend->block_add_statements (block, stmts);

    return block;
  }

  Bblock *peek_enclosing_scope ()
  {
    if (scope_stack.size () == 0)
      return nullptr;

    return scope_stack.back ();
  }

  void add_statement (Bstatement *stmt) { statements.back ().push_back (stmt); }

  void insert_var_decl (HirId id, ::Bvariable *decl)
  {
    compiled_var_decls[id] = decl;
  }

  bool lookup_var_decl (HirId id, ::Bvariable **decl)
  {
    auto it = compiled_var_decls.find (id);
    if (it == compiled_var_decls.end ())
      return false;

    *decl = it->second;
    return true;
  }

  void insert_function_decl (HirId id, ::Bfunction *fn)
  {
    compiled_fn_map[id] = fn;
  }

  bool lookup_function_decl (HirId id, ::Bfunction **fn)
  {
    auto it = compiled_fn_map.find (id);
    if (it == compiled_fn_map.end ())
      return false;

    *fn = it->second;
    return true;
  }

  void insert_const_decl (HirId id, ::Bexpression *expr)
  {
    compiled_consts[id] = expr;
  }

  bool lookup_const_decl (HirId id, ::Bexpression **expr)
  {
    auto it = compiled_consts.find (id);
    if (it == compiled_consts.end ())
      return false;

    *expr = it->second;
    return true;
  }

  void push_fn (::Bfunction *fn, ::Bvariable *ret_addr)
  {
    fn_stack.push_back (fncontext{fn, ret_addr});
  }
  void pop_fn () { fn_stack.pop_back (); }
  fncontext peek_fn () { return fn_stack.back (); }

  void push_type (::Btype *t) { type_decls.push_back (t); }
  void push_var (::Bvariable *v) { var_decls.push_back (v); }
  void push_const (::Bexpression *c) { const_decls.push_back (c); }
  void push_function (::Bfunction *f) { func_decls.push_back (f); }

  void write_to_backend ()
  {
    backend->write_global_definitions (type_decls, const_decls, func_decls,
				       var_decls);
  }

  bool function_completed (Bfunction *fn)
  {
    for (auto it = func_decls.begin (); it != func_decls.end (); it++)
      {
	Bfunction *i = (*it);
	if (i == fn)
	  {
	    return true;
	  }
      }
    return false;
  }

private:
  ::Backend *backend;
  Resolver::Resolver *resolver;
  Resolver::TypeCheckContext *tyctx;
  Analysis::Mappings *mappings;

  // state
  std::vector<fncontext> fn_stack;
  std::map<HirId, ::Bvariable *> compiled_var_decls;
  std::map<HirId, ::Btype *> compiled_type_map;
  std::map<HirId, ::Bfunction *> compiled_fn_map;
  std::map<HirId, ::Bexpression *> compiled_consts;
  std::vector< ::std::vector<Bstatement *> > statements;
  std::vector< ::Bblock *> scope_stack;

  // To GCC middle-end
  std::vector< ::Btype *> type_decls;
  std::vector< ::Bvariable *> var_decls;
  std::vector< ::Bexpression *> const_decls;
  std::vector< ::Bfunction *> func_decls;
};

class TyTyResolveCompile : public TyTy::TyVisitor
{
public:
  static ::Btype *compile (Context *ctx, TyTy::TyBase *ty)
  {
    TyTyResolveCompile compiler (ctx);
    ty->accept_vis (compiler);
    return compiler.translated;
  }

  virtual ~TyTyResolveCompile () {}

  void visit (TyTy::ErrorType &type) override { gcc_unreachable (); }

  void visit (TyTy::UnitType &type) override { gcc_unreachable (); }

  void visit (TyTy::InferType &type) override { gcc_unreachable (); }

  void visit (TyTy::FnType &type) override { gcc_unreachable (); }

  void visit (TyTy::StructFieldType &type) override { gcc_unreachable (); }

  void visit (TyTy::ParamType &type) override { gcc_unreachable (); }

  void visit (TyTy::ADTType &type) override
  {
    ::Btype *compiled_type = nullptr;
    bool ok = ctx->lookup_compiled_types (type.get_ref (), &compiled_type);
    rust_assert (ok);
    translated = compiled_type;
  }

  void visit (TyTy::ArrayType &type) override
  {
    mpz_t ival;
    mpz_init_set_ui (ival, type.get_capacity ());

    Btype *capacity_type = ctx->get_backend ()->integer_type (true, 32);
    Bexpression *length
      = ctx->get_backend ()->integer_constant_expression (capacity_type, ival);

    Btype *element_type = TyTyResolveCompile::compile (ctx, type.get_type ());
    translated = ctx->get_backend ()->array_type (element_type, length);
  }

  void visit (TyTy::BoolType &type) override
  {
    ::Btype *compiled_type = nullptr;
    bool ok = ctx->lookup_compiled_types (type.get_ref (), &compiled_type);
    rust_assert (ok);
    translated = compiled_type;
  }

  void visit (TyTy::IntType &type) override
  {
    ::Btype *compiled_type = nullptr;
    bool ok = ctx->lookup_compiled_types (type.get_ref (), &compiled_type);
    rust_assert (ok);
    translated = compiled_type;
  }

  void visit (TyTy::UintType &type) override
  {
    ::Btype *compiled_type = nullptr;
    bool ok = ctx->lookup_compiled_types (type.get_ref (), &compiled_type);
    rust_assert (ok);
    translated = compiled_type;
  }

  void visit (TyTy::FloatType &type) override
  {
    ::Btype *compiled_type = nullptr;
    bool ok = ctx->lookup_compiled_types (type.get_ref (), &compiled_type);
    rust_assert (ok);
    translated = compiled_type;
  }

private:
  TyTyResolveCompile (Context *ctx) : ctx (ctx) {}

  Context *ctx;
  ::Btype *translated;
};

} // namespace Compile
} // namespace Rust

#endif // RUST_COMPILE_CONTEXT
