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

#ifndef RUST_HIR_TYPE_CHECK_EXPR
#define RUST_HIR_TYPE_CHECK_EXPR

#include "rust-hir-type-check-base.h"
#include "rust-hir-full.h"
#include "rust-tyty.h"
#include "rust-tyty-call.h"
#include "rust-tyty-resolver.h"
#include "rust-hir-type-check-struct-field.h"

namespace Rust {
namespace Resolver {

class TypeCheckExpr : public TypeCheckBase
{
public:
  static TyTy::TyBase *Resolve (HIR::Expr *expr, bool is_final_expr = false)
  {
    TypeCheckExpr resolver (is_final_expr);
    expr->accept_vis (resolver);
    if (resolver.infered != nullptr)
      resolver.context->insert_type (expr->get_mappings ().get_hirid (),
				     resolver.infered);

    return resolver.infered;
  }

  void visit (HIR::ReturnExpr &expr)
  {
    auto ret = context->peek_return_type ();
    rust_assert (ret != nullptr);

    auto expr_ty = TypeCheckExpr::Resolve (expr.get_expr ());
    infered = ret->combine (expr_ty);
  }

  void visit (HIR::CallExpr &expr)
  {
    auto fn = expr.get_fnexpr ();
    auto fn_node_id = fn->get_mappings ().get_nodeid ();

    // then lookup the reference_node_id
    NodeId ref_node_id;
    if (!resolver->lookup_resolved_name (fn_node_id, &ref_node_id))
      {
	rust_error_at (expr.get_locus (),
		       "Failed to lookup reference for node: %s",
		       expr.as_string ().c_str ());
	return;
      }

    // node back to HIR
    HirId ref;
    if (!mappings->lookup_node_to_hir (expr.get_mappings ().get_crate_num (),
				       ref_node_id, &ref))
      {
	rust_error_at (expr.get_locus (), "reverse lookup failure");
	return;
      }

    // check if this has a type
    TyTy::TyBase *lookup;
    if (!context->lookup_type (ref, &lookup))
      {
	// FIXME we need to be able to lookup the location info for the
	// reference here
	rust_error_at (expr.get_locus (), "consider giving this a type: %s",
		       expr.as_string ().c_str ());
	return;
      }

    infered = TyTy::TypeCheckCallExpr::go (lookup, expr);
  }

  void visit (HIR::AssignmentExpr &expr)
  {
    auto lhs = TypeCheckExpr::Resolve (expr.get_lhs ());
    auto rhs = TypeCheckExpr::Resolve (expr.get_rhs ());

    infered = lhs->combine (rhs);
    // need to overrite the lhs type with this combination
    context->insert_type (expr.get_lhs ()->get_mappings ().get_hirid (),
			  infered);
  }

  void visit (HIR::IdentifierExpr &expr)
  {
    NodeId ast_node_id = expr.get_mappings ().get_nodeid ();

    // then lookup the reference_node_id
    NodeId ref_node_id;
    if (!resolver->lookup_resolved_name (ast_node_id, &ref_node_id))
      {
	rust_error_at (expr.get_locus (),
		       "Failed to lookup reference for node: %s",
		       expr.as_string ().c_str ());
	return;
      }

    // these ref_node_ids will resolve to a pattern declaration but we are
    // interested in the definition that this refers to get the parent id
    Definition def;
    if (!resolver->lookup_definition (ref_node_id, &def))
      {
	rust_error_at (expr.get_locus (), "unknown reference");
	return;
      }

    // node back to HIR
    HirId ref;
    if (!mappings->lookup_node_to_hir (expr.get_mappings ().get_crate_num (),
				       def.parent, &ref))
      {
	rust_error_at (expr.get_locus (), "reverse lookup failure");
	return;
      }

    // the base reference for this name _must_ have a type set
    TyTy::TyBase *lookup;
    if (!context->lookup_type (ref, &lookup))
      {
	rust_error_at (mappings->lookup_location (ref),
		       "consider giving this a type: %s",
		       expr.as_string ().c_str ());
	return;
      }

    // FIXME this needs to be cloned for memory management later on
    infered = lookup;
  }

  void visit (HIR::LiteralExpr &expr)
  {
    switch (expr.get_lit_type ())
      {
	case HIR::Literal::LitType::INT: {
	  bool ok = false;

	  switch (expr.get_literal ()->get_type_hint ())
	    {
	    case CORETYPE_I8:
	      ok = context->lookup_builtin ("i8", &infered);
	      break;
	    case CORETYPE_I16:
	      ok = context->lookup_builtin ("i16", &infered);
	      break;
	    case CORETYPE_I32:
	      ok = context->lookup_builtin ("i32", &infered);
	      break;
	    case CORETYPE_I64:
	      ok = context->lookup_builtin ("i64", &infered);
	      break;
	    case CORETYPE_I128:
	      ok = context->lookup_builtin ("i128", &infered);
	      break;

	    case CORETYPE_U8:
	      ok = context->lookup_builtin ("u8", &infered);
	      break;
	    case CORETYPE_U16:
	      ok = context->lookup_builtin ("u16", &infered);
	      break;
	    case CORETYPE_U32:
	      ok = context->lookup_builtin ("u32", &infered);
	      break;
	    case CORETYPE_U64:
	      ok = context->lookup_builtin ("u64", &infered);
	      break;
	    case CORETYPE_U128:
	      ok = context->lookup_builtin ("u128", &infered);
	      break;

	    case CORETYPE_F32:
	      expr.get_literal ()->set_lit_type (HIR::Literal::LitType::FLOAT);
	      ok = context->lookup_builtin ("f32", &infered);
	      break;
	    case CORETYPE_F64:
	      expr.get_literal ()->set_lit_type (HIR::Literal::LitType::FLOAT);
	      ok = context->lookup_builtin ("f64", &infered);
	      break;

	    default:
	      ok = context->lookup_builtin ("i32", &infered);
	      break;
	    }
	  rust_assert (ok);
	}
	break;

	case HIR::Literal::LitType::FLOAT: {
	  bool ok = false;

	  switch (expr.get_literal ()->get_type_hint ())
	    {
	    case CORETYPE_F32:
	      ok = context->lookup_builtin ("f32", &infered);
	      break;
	    case CORETYPE_F64:
	      ok = context->lookup_builtin ("f64", &infered);
	      break;
	    default:
	      ok = context->lookup_builtin ("f32", &infered);
	      break;
	    }
	  rust_assert (ok);
	}
	break;

	case HIR::Literal::LitType::BOOL: {
	  auto ok = context->lookup_builtin ("bool", &infered);
	  rust_assert (ok);
	}
	break;

      default:
	gcc_unreachable ();
	break;
      }
  }

  void visit (HIR::ArithmeticOrLogicalExpr &expr)
  {
    auto lhs = TypeCheckExpr::Resolve (expr.get_lhs ());
    auto rhs = TypeCheckExpr::Resolve (expr.get_rhs ());

    infered = lhs->combine (rhs);
  }

  void visit (HIR::ComparisonExpr &expr)
  {
    auto lhs = TypeCheckExpr::Resolve (expr.get_lhs ());
    auto rhs = TypeCheckExpr::Resolve (expr.get_rhs ());

    infered = lhs->combine (rhs);
    // FIXME this will need to turn into bool
  }

  void visit (HIR::LazyBooleanExpr &expr)
  {
    auto lhs = TypeCheckExpr::Resolve (expr.get_lhs ());
    auto rhs = TypeCheckExpr::Resolve (expr.get_rhs ());

    infered = lhs->combine (rhs);
    // FIXME this will need to turn into bool
  }

  void visit (HIR::IfExpr &expr)
  {
    TypeCheckExpr::Resolve (expr.get_if_condition ());
    TypeCheckExpr::Resolve (expr.get_if_block ());

    // if without else always resolves to unit type
    infered = new TyTy::UnitType (expr.get_mappings ().get_hirid ());
  }

  void visit (HIR::IfExprConseqElse &expr)
  {
    // this must combine to what the type is expected
    // this might be a parameter or the last expr in an if + else in a BlockExpr
    // then it must resolve to fn return type
    // else its a unit-type
    infered = is_final_expr
		? context->peek_return_type ()
		: new TyTy::UnitType (expr.get_mappings ().get_hirid ());

    TypeCheckExpr::Resolve (expr.get_if_condition (), is_final_expr);
    auto if_blk_ty = TypeCheckExpr::Resolve (expr.get_if_block ());
    auto else_blk_ty = TypeCheckExpr::Resolve (expr.get_else_block ());

    if (is_final_expr)
      {
	infered = infered->combine (if_blk_ty);
	infered = infered->combine (else_blk_ty);
      }
  }

  void visit (HIR::IfExprConseqIf &expr)
  {
    TypeCheckExpr::Resolve (expr.get_if_condition ());
    TypeCheckExpr::Resolve (expr.get_if_block ());
    TypeCheckExpr::Resolve (expr.get_conseq_if_expr ());

    infered = new TyTy::UnitType (expr.get_mappings ().get_hirid ());
  }

  void visit (HIR::BlockExpr &expr);

  void visit (HIR::ArrayIndexExpr &expr)
  {
    // FIXME this should be size type
    TyTy::IntType size_ty (expr.get_index_expr ()->get_mappings ().get_hirid (),
			   TyTy::IntType::I32);
    auto resolved
      = size_ty.combine (TypeCheckExpr::Resolve (expr.get_index_expr ()));
    rust_assert (resolved != nullptr);

    expr.get_array_expr ()->accept_vis (*this);
    if (infered->get_kind () != TyTy::TypeKind::ARRAY)
      {
	rust_fatal_error (expr.get_array_expr ()->get_locus_slow (),
			  "expected an ArrayType for index expression");
	return;
      }

    // extract the element type out now from the base type
    infered = TyTyExtractorArray::ExtractElementTypeFromArray (infered);
  }

  void visit (HIR::ArrayExpr &expr)
  {
    HIR::ArrayElems *elements = expr.get_internal_elements ();
    size_t num_elems = elements->get_num_elements ();

    elements->accept_vis (*this);
    rust_assert (infered_array_elems != nullptr);

    infered = new TyTy::ArrayType (expr.get_mappings ().get_hirid (), num_elems,
				   infered_array_elems);
  }

  void visit (HIR::ArrayElemsValues &elems)
  {
    std::vector<TyTy::TyBase *> types;
    elems.iterate ([&] (HIR::Expr *e) mutable -> bool {
      types.push_back (TypeCheckExpr::Resolve (e));
      return true;
    });

    infered_array_elems = types[0];
    for (size_t i = 1; i < types.size (); i++)
      {
	infered_array_elems = infered_array_elems->combine (types.at (i));
      }
  }

  void visit (HIR::ArrayElemsCopied &elems)
  {
    infered_array_elems = TypeCheckExpr::Resolve (elems.get_elem_to_copy ());
  }

  void visit (HIR::StructExprStructFields &struct_expr)
  {
    infered = TypeCheckStructExpr::Resolve (&struct_expr);
  }

private:
  TypeCheckExpr (bool is_final_expr)
    : TypeCheckBase (), infered (nullptr), infered_array_elems (nullptr),
      is_final_expr (is_final_expr)
  {}

  TyTy::TyBase *infered;
  TyTy::TyBase *infered_array_elems;

  bool is_final_expr;
};

} // namespace Resolver
} // namespace Rust

#endif // RUST_HIR_TYPE_CHECK_EXPR
