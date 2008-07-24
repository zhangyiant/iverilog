/*
 *  VHDL abstract syntax elements.
 *
 *  Copyright (C) 2008  Nick Gasson (nick@nickg.me.uk)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "vhdl_syntax.hh"
#include "vhdl_helper.hh"

#include <cassert>
#include <iostream>
#include <typeinfo>

vhdl_scope::vhdl_scope()
   : parent_(NULL), init_(false), sig_assign_(true)
{
   
}

vhdl_scope::~vhdl_scope()
{
   delete_children<vhdl_decl>(decls_);
}

void vhdl_scope::set_initializing(bool i)
{
   init_ = i;
   if (parent_)
      parent_->set_initializing(i);
}

void vhdl_scope::add_decl(vhdl_decl *decl)
{
   decls_.push_back(decl);
}

vhdl_decl *vhdl_scope::get_decl(const std::string &name) const
{
   decl_list_t::const_iterator it;
   for (it = decls_.begin(); it != decls_.end(); ++it) {
      if ((*it)->get_name() == name)
         return *it;
   }

   return parent_ ? parent_->get_decl(name) : NULL;
}

bool vhdl_scope::have_declared(const std::string &name) const
{
   return get_decl(name) != NULL;
}

vhdl_scope *vhdl_scope::get_parent() const
{
   assert(parent_);
   return parent_;
}

vhdl_entity::vhdl_entity(const char *name, const char *derived_from,
                         vhdl_arch *arch)
   : name_(name), arch_(arch), derived_from_(derived_from)
{
   arch->get_scope()->set_parent(&ports_);
}

vhdl_entity::~vhdl_entity()
{   
   delete arch_;
}

void vhdl_entity::add_port(vhdl_port_decl *decl)
{
   ports_.add_decl(decl);
}

void vhdl_entity::emit(std::ostream &of, int level) const
{
   // Pretty much every design will use std_logic so we
   // might as well include it by default
   of << "library ieee;" << std::endl;
   of << "use ieee.std_logic_1164.all;" << std::endl;
   of << "use ieee.numeric_std.all;" << std::endl;
   of << "use std.textio.all;" << std::endl;
   //of << "use work.verilog_support.all;" << std::endl;
   of << std::endl;
   
   emit_comment(of, level);
   of << "entity " << name_ << " is";

   if (!ports_.empty()) {
      newline(of, indent(level));
      of << "port (";
      emit_children<vhdl_decl>(of, ports_.get_decls(), indent(level), ";");
      of << ");";
   }
   
   newline(of, level);
   of << "end entity; ";
   blank_line(of, level);  // Extra blank line after entities
   arch_->emit(of, level);
}

vhdl_arch::~vhdl_arch()
{
   delete_children<vhdl_conc_stmt>(stmts_);
}

void vhdl_arch::add_stmt(vhdl_process *proc)
{
   proc->get_scope()->set_parent(&scope_);
   stmts_.push_back(proc);
}

void vhdl_arch::add_stmt(vhdl_conc_stmt *stmt)
{
   stmts_.push_back(stmt);
}

void vhdl_arch::emit(std::ostream &of, int level) const
{
   emit_comment(of, level);
   of << "architecture " << name_ << " of " << entity_;
   of << " is";
   emit_children<vhdl_decl>(of, scope_.get_decls(), level);
   of << "begin";
   emit_children<vhdl_conc_stmt>(of, stmts_, level);
   of << "end architecture;";
   blank_line(of, level);  // Extra blank line after architectures;
}

void vhdl_process::add_sensitivity(const char *name)
{
   sens_.push_back(name);
}

void vhdl_process::emit(std::ostream &of, int level) const
{
   // If there are no statements in the body, this process
   // can't possibly do anything, so don't bother to emit it
   if (stmts_.empty()) {
      of << "-- Removed one empty process";
      newline(of, level);
      return;
   }
   
   emit_comment(of, level);
   if (name_.size() > 0)
      of << name_ << ": ";
   of << "process ";
   
   int num_sens = sens_.size();
   if (num_sens > 0) {
      of << "(";
      string_list_t::const_iterator it;
      for (it = sens_.begin(); it != sens_.end(); ++it) {
         of << *it;
         if (--num_sens > 0)
            of << ", ";
      }
      of << ") ";
   }

   of << "is";
   emit_children<vhdl_decl>(of, scope_.get_decls(), level);
   of << "begin";
   stmts_.emit(of, level);
   of << "end process;";
   newline(of, level);
}

stmt_container::~stmt_container()
{
   delete_children<vhdl_seq_stmt>(stmts_);
}

void stmt_container::add_stmt(vhdl_seq_stmt *stmt)
{
   stmts_.push_back(stmt);
}

void stmt_container::emit(std::ostream &of, int level, bool newline) const
{
   emit_children<vhdl_seq_stmt>(of, stmts_, level, "", newline);  
}

vhdl_comp_inst::vhdl_comp_inst(const char *inst_name, const char *comp_name)
   : comp_name_(comp_name), inst_name_(inst_name)
{
   
}

vhdl_comp_inst::~vhdl_comp_inst()
{
   port_map_list_t::iterator it;
   for (it = mapping_.begin(); it != mapping_.end(); ++it) {
      delete (*it).expr;
   }
   mapping_.clear();
}

void vhdl_comp_inst::map_port(const char *name, vhdl_expr *expr)
{
   port_map_t pmap = { name, expr };
   mapping_.push_back(pmap);
}

void vhdl_comp_inst::emit(std::ostream &of, int level) const
{
   emit_comment(of, level);
   of << inst_name_ << ": " << comp_name_;

   // If there are no ports or generics we don't need to mention them...
   if (mapping_.size() > 0) {
      newline(of, indent(level));
      of << "port map (";

      int sz = mapping_.size();
      port_map_list_t::const_iterator it;
      for (it = mapping_.begin(); it != mapping_.end(); ++it) {
         newline(of, indent(indent(level)));
         of << (*it).name << " => ";
         (*it).expr->emit(of, level);
         if (--sz > 0)
            of << ",";
      }
      newline(of, indent(level));
      of << ")";
   }
   
   of << ";";
   newline(of, level);
}

vhdl_component_decl::vhdl_component_decl(const char *name)
   : vhdl_decl(name)
{

}

/*
 * Create a component declaration for the given entity.
 */
vhdl_component_decl *vhdl_component_decl::component_decl_for(vhdl_entity *ent)
{
   assert(ent != NULL);

   vhdl_component_decl *decl = new vhdl_component_decl
      (ent->get_name().c_str());

   decl->ports_ = ent->get_scope()->get_decls();
   
   return decl;
}

void vhdl_component_decl::emit(std::ostream &of, int level) const
{
   emit_comment(of, level);
   of << "component " << name_ << " is";

   if (ports_.size() > 0) {
      newline(of, indent(level));
      of << "port (";
      emit_children<vhdl_decl>(of, ports_, indent(level), ";");
      of << ");";
   }
   
   newline(of, level);
   of << "end component;";
}

vhdl_wait_stmt::~vhdl_wait_stmt()
{
   if (expr_ != NULL)
      delete expr_;
}

void vhdl_wait_stmt::emit(std::ostream &of, int level) const
{
   of << "wait";

   switch (type_) {
   case VHDL_WAIT_INDEF:
      break;
   case VHDL_WAIT_FOR:
      assert(expr_);
      of << " for ";
      expr_->emit(of, level);
      break;
   case VHDL_WAIT_UNTIL:
      assert(expr_);
      of << " until ";
      expr_->emit(of, level);
      break;
   case VHDL_WAIT_ON:
      {
         of << " on ";
         string_list_t::const_iterator it = sensitivity_.begin();
         while (it != sensitivity_.end()) {
            of << *it;
            if (++it != sensitivity_.end())
               of << ", ";
         }
      }
      break;
   }
   
   of << ";";
}

vhdl_decl::~vhdl_decl()
{
   if (type_ != NULL)
      delete type_;
   if (initial_ != NULL)
      delete initial_;
}

const vhdl_type *vhdl_decl::get_type() const
{
   assert(type_);
   return type_;
}

void vhdl_decl::set_initial(vhdl_expr *initial)
{   
   if (!has_initial_) {
      assert(initial_ == NULL);
      initial_ = initial;
      has_initial_ = true;
   }
}

void vhdl_port_decl::emit(std::ostream &of, int level) const
{
   of << name_ << " : ";
   
   switch (mode_) {
   case VHDL_PORT_IN:
      of << "in ";
      break;
   case VHDL_PORT_OUT:
      of << "out ";
      break;
   case VHDL_PORT_INOUT:
      of << "inout ";
      break;
   }
   
   type_->emit(of, level);
}

void vhdl_var_decl::emit(std::ostream &of, int level) const
{
   of << "variable " << name_ << " : ";
   type_->emit(of, level);
   
   if (initial_) {
      of << " := ";
      initial_->emit(of, level);
   }
       
   of << ";";
   emit_comment(of, level, true);
}

void vhdl_signal_decl::emit(std::ostream &of, int level) const
{
   of << "signal " << name_ << " : ";
   type_->emit(of, level);
   
   if (initial_) {
      of << " := ";
      initial_->emit(of, level);
   }
       
   of << ";";
   emit_comment(of, level, true);
}

void vhdl_type_decl::emit(std::ostream &of, int level) const
{
   of << "type " << name_ << " is ";
   of << type_->get_type_decl_string() << ";";
   emit_comment(of, level, true);
}

vhdl_expr::~vhdl_expr()
{
   if (type_ != NULL)
      delete type_;
}

void vhdl_expr_list::add_expr(vhdl_expr *e)
{
   exprs_.push_back(e);
}

vhdl_expr_list::~vhdl_expr_list()
{
   delete_children<vhdl_expr>(exprs_);
}

void vhdl_expr_list::emit(std::ostream &of, int level) const
{
   of << "(";
   
   int size = exprs_.size();
   std::list<vhdl_expr*>::const_iterator it;
   for (it = exprs_.begin(); it != exprs_.end(); ++it) {
      (*it)->emit(of, level);
      if (--size > 0)
         of << ", ";
   }

   of << ")";
}

void vhdl_pcall_stmt::emit(std::ostream &of, int level) const
{
   of << name_;
   if (!exprs_.empty())
      exprs_.emit(of, level);
   of << ";";
}

vhdl_var_ref::~vhdl_var_ref()
{
   if (slice_)
      delete slice_;
}

void vhdl_var_ref::set_slice(vhdl_expr *s, int w)
{
   assert(type_);

   slice_ = s;
   slice_width_ = w;
      
   vhdl_type_name_t tname = type_->get_name();
   if (tname == VHDL_TYPE_ARRAY) {
      type_ = new vhdl_type(*type_->get_base());
   }
   else {
      assert(tname == VHDL_TYPE_UNSIGNED || tname == VHDL_TYPE_SIGNED);

      if (type_)
         delete type_;
      
      if (w > 0)
         type_ = new vhdl_type(tname, w);
      else
         type_ = vhdl_type::std_logic();   
   }
}
   
void vhdl_var_ref::emit(std::ostream &of, int level) const
{
   of << name_;
   if (slice_) {
      of << "(";
      if (slice_width_ > 0) {
         slice_->emit(of, level);
         of << " + " << slice_width_ << " downto ";
      }
      slice_->emit(of, level);
      of << ")";
   }
}

void vhdl_const_string::emit(std::ostream &of, int level) const
{
   // In some instances a string literal can be ambiguous between
   // a String type and some other types (e.g. std_logic_vector)
   // The explicit cast to String removes this ambiguity (although
   // isn't always strictly necessary) 
   of << "String'(\"" << value_ << "\")";
}

void vhdl_null_stmt::emit(std::ostream &of, int level) const
{
   of << "null;";
}

void vhdl_fcall::emit(std::ostream &of, int level) const
{
   of << name_;
   exprs_.emit(of, level);
}

vhdl_abstract_assign_stmt::~vhdl_abstract_assign_stmt()
{
   delete lhs_;
   delete rhs_;
   if (after_)
      delete after_;
}

void vhdl_nbassign_stmt::emit(std::ostream &of, int level) const
{
   lhs_->emit(of, level);
   of << " <= ";
   rhs_->emit(of, level);

   if (after_) {
      of << " after ";
      after_->emit(of, level);
   }
   
   of << ";";
}

void vhdl_assign_stmt::emit(std::ostream &of, int level) const
{
   lhs_->emit(of, level);
   of << " := ";
   rhs_->emit(of, level);
   of << ";";
}

vhdl_const_bits::vhdl_const_bits(const char *value, int width, bool issigned)   
   : vhdl_expr(issigned ? vhdl_type::nsigned(width)
               : vhdl_type::nunsigned(width), true),
     qualified_(false),
     signed_(issigned)
{   
   // Can't rely on value being NULL-terminated
   while (width--)
      value_.push_back(*value++);
}

void vhdl_const_bits::emit(std::ostream &of, int level) const
{
   if (qualified_)
      of << (signed_ ? "signed" : "unsigned") << "'(\"";
   else
      of << "\"";

   // The bits appear to be in reverse order
   std::string::const_reverse_iterator it;
   for (it = value_.rbegin(); it != value_.rend(); ++it)
      of << vl_to_vhdl_bit(*it);

   of << (qualified_ ? "\")" : "\"");
}

void vhdl_const_bit::emit(std::ostream &of, int level) const
{
   of << "'" << vl_to_vhdl_bit(bit_) << "'";
}

void vhdl_const_int::emit(std::ostream &of, int level) const
{
   of << value_;
}

void vhdl_const_time::emit(std::ostream &of, int level) const
{
   of << value_;
   switch (units_) {
   case TIME_UNIT_NS:
      of << " ns";
   }
}

vhdl_cassign_stmt::~vhdl_cassign_stmt()
{
   delete lhs_;
   delete rhs_;

   for (std::list<when_part_t>::const_iterator it = whens_.begin();
        it != whens_.end();
        ++it) {
      delete (*it).value;
      delete (*it).cond;
   }
}

void vhdl_cassign_stmt::add_condition(vhdl_expr *value, vhdl_expr *cond)
{
   when_part_t when = { value, cond };
   whens_.push_back(when);
}

void vhdl_cassign_stmt::emit(std::ostream &of, int level) const
{
   lhs_->emit(of, level);
   of << " <= ";
   if (!whens_.empty()) {
      for (std::list<when_part_t>::const_iterator it = whens_.begin();
           it != whens_.end();
           ++it) {
         (*it).value->emit(of, level);
         of << " when ";
         (*it).cond->emit(of, level);
         of << " ";
      }
      of << "else ";
   }
   rhs_->emit(of, level);
   
   if (after_) {
      of << " after ";
      after_->emit(of, level);
   }
      
   of << ";";
}

void vhdl_assert_stmt::emit(std::ostream &of, int level) const
{
   of << "assert false";  // TODO: Allow arbitrary expression 
   of << " report \"" << reason_ << "\" severity failure;";
}

vhdl_if_stmt::vhdl_if_stmt(vhdl_expr *test)
{
   // Need to ensure that the expression is Boolean
   vhdl_type boolean(VHDL_TYPE_BOOLEAN);
   test_ = test->cast(&boolean);
}

vhdl_if_stmt::~vhdl_if_stmt()
{
   delete test_;
}

void vhdl_if_stmt::emit(std::ostream &of, int level) const
{
   of << "if ";
   test_->emit(of, level);
   of << " then";
   then_part_.emit(of, level);
   if (!else_part_.empty()) {
      of << "else";
      else_part_.emit(of, level);
   }
   of << "end if;";
}

vhdl_unaryop_expr::~vhdl_unaryop_expr()
{
   delete operand_;
}

void vhdl_unaryop_expr::emit(std::ostream &of, int level) const
{
   of << "(";
   switch (op_) {
   case VHDL_UNARYOP_NOT:
      of << "not ";
      break;
   case VHDL_UNARYOP_NEG:
      of << "-";
      break;
   }
   operand_->emit(of, level);
   of << ")";
}

vhdl_binop_expr::vhdl_binop_expr(vhdl_expr *left, vhdl_binop_t op,
                                 vhdl_expr *right, vhdl_type *type)
   : vhdl_expr(type), op_(op)
{
   add_expr(left);
   add_expr(right);
}

vhdl_binop_expr::~vhdl_binop_expr()
{
   delete_children<vhdl_expr>(operands_);
}

void vhdl_binop_expr::add_expr(vhdl_expr *e)
{
   operands_.push_back(e);
}

void vhdl_binop_expr::emit(std::ostream &of, int level) const
{
   // Expressions are fully parenthesized to remove any
   // ambiguity in the output

   of << "(";

   assert(operands_.size() > 0);   
   std::list<vhdl_expr*>::const_iterator it = operands_.begin();

   (*it)->emit(of, level);
   while (++it != operands_.end()) {
      const char* ops[] = {
         "and", "or", "=", "/=", "+", "-", "*", "<",
         ">", "<=", ">=", "sll", "srl", "xor", "&"
      };

      of << " " << ops[op_] << " ";

      (*it)->emit(of, level);
   }      

   of << ")";
}

vhdl_case_branch::~vhdl_case_branch()
{
   delete when_;
}

void vhdl_case_branch::emit(std::ostream &of, int level) const
{
   of << "when ";
   when_->emit(of, level);
   of << " =>";
   stmts_.emit(of, indent(level), false);
}

vhdl_case_stmt::~vhdl_case_stmt()
{
   delete test_;
}

void vhdl_case_stmt::emit(std::ostream &of, int level) const
{
   of << "case ";
   test_->emit(of, level);
   of << " is";
   newline(of, indent(level));

   case_branch_list_t::const_iterator it;
   int n = branches_.size();
   for (it = branches_.begin(); it != branches_.end(); ++it) {
      (*it)->emit(of, level);
      if (--n > 0)
         newline(of, indent(level));
      else
         newline(of, level);
   }
   
   of << "end case;";
}

vhdl_while_stmt::~vhdl_while_stmt()
{
   delete test_;
}

void vhdl_while_stmt::emit(std::ostream &of, int level) const
{
   of << "while ";
   test_->emit(of, level);
   of << " loop";
   stmts_.emit(of, level);
   of << "end loop;";
}

void vhdl_loop_stmt::emit(std::ostream &of, int level) const
{
   of << "loop";
   stmts_.emit(of, level);
   of << "end loop;";
}

vhdl_for_stmt::~vhdl_for_stmt()
{
   delete from_;
   delete to_;
}

void vhdl_for_stmt::emit(std::ostream &of, int level) const
{
   of << "for " << lname_ << " in ";
   from_->emit(of, level);
   of << " to ";
   to_->emit(of, level);
   of << " ";
   loop_.emit(of, level);
}

vhdl_function::vhdl_function(const char *name, vhdl_type *ret_type)
   : vhdl_decl(name, ret_type)
{
   // A function contains two scopes:
   //  scope_ = The paramters
   //  variables_ = Local variables
   // A call to get_scope returns variables_ whose parent is scope_
   variables_.set_parent(&scope_);
}

void vhdl_function::emit(std::ostream &of, int level) const
{
   of << "function " << name_ << " (";
   emit_children<vhdl_decl>(of, scope_.get_decls(), level, ";");
   of << ") ";
   newline(of, level);
   of << "return " << type_->get_string() << " is";
   emit_children<vhdl_decl>(of, variables_.get_decls(), level);
   of << "begin";
   stmts_.emit(of, level);
   of << "  return Verilog_Result;";
   newline(of, level);
   of << "end function;";
   newline(of, level);
}

void vhdl_param_decl::emit(std::ostream &of, int level) const
{
   of << name_ << " : ";
   type_->emit(of, level);
}
