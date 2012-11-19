
/*
    defination of QBASIC Abstruct Syntax Tree - The Type System
    Copyright (C) 2012  microcai <microcai@fedoraproject.org>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 3 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <iostream>
#include <boost/lexical_cast.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/foreach.hpp>
#include <boost/assert.hpp>

#include <llvm/DerivedTypes.h>
#include <llvm/Constants.h>
#include <llvm/Constant.h>
#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Instructions.h>
#include <llvm/Instruction.h>
#include <llvm/Support/IRBuilder.h>

#include "llvmwrapper.hpp"
#include "ast.hpp"
#include "type.hpp"

#define debug std::printf

//static map of the internal type system
static	NumberExprTypeAST	_numbertype;
static	StringExprTypeAST	_stringtype;
static	VoidExprTypeAST		_voidtype;
static	ExprTypeASTPtr numbertype(&_numbertype);
static	ExprTypeASTPtr stringtype(&_stringtype);
static	ExprTypeASTPtr voidtype(&_voidtype);

ExprTypeASTPtr VoidExprTypeAST::GetVoidExprTypeAST()
{
	return voidtype;
}

ExprTypeASTPtr NumberExprTypeAST::GetNumberExprTypeAST()
{
	return numbertype;
}

ExprTypeASTPtr StringExprTypeAST::GetStringExprTypeAST()
{
	return stringtype;
}


ExprTypeAST* ConstNumberExprAST::type(ASTContext)
{
    return &_numbertype;
}

ExprTypeAST* ConstStringExprAST::type(ASTContext)
{
	return &_stringtype;
}

ExprTypeAST* VariableExprAST::type(ASTContext ctx)
{
	return nameresolve(ctx)->type.get();
}

ExprTypeAST* AssignmentExprAST::type(ASTContext ctx)
{
	//TODO, result the type
	return lval->type(ctx);
}

ExprTypeAST* CalcExprAST::type(ASTContext ctx)
{
	//类型是左边的操作符的类型
	return lval->type(ctx);
}

ExprTypeAST* CallExprAST::type(ASTContext ctx)
{	
	return nameresolve(ctx)->type.get();
}

llvm::Type* VoidExprTypeAST::llvm_type(ASTContext ctx)
{
	llvm::Type::getVoidTy(ctx.module->getContext());
}

llvm::Type* NumberExprTypeAST::llvm_type(ASTContext ctx)
{
	switch(sizeof(long)){
		case 8:
			return llvm::Type::getInt64Ty(ctx.module->getContext());
		case 4:
			return llvm::Type::getInt32Ty(llvm::getGlobalContext());
	}
	return llvm::Type::getInt32Ty(llvm::getGlobalContext());
}

llvm::Type* StringExprTypeAST::llvm_type(ASTContext ctx)
{
	return llvm::Type::getInt8PtrTy(llvm::getGlobalContext());
}

llvm::Value* NumberExprTypeAST::Alloca(ASTContext ctx, const std::string _name)
{
	debug("allocation for value %s type long\n",_name.c_str());
	//TODO , 在堆栈上分配个变量
	llvm::IRBuilder<> builder(&ctx.llvmfunc->getEntryBlock(),
							  ctx.llvmfunc->getEntryBlock().begin());

	builder.SetInsertPoint(ctx.block);

	return builder.CreateAlloca(this->llvm_type(ctx),0,_name);
}

llvm::Value* StringExprTypeAST::Alloca(ASTContext ctx, const std::string _name)
{
	debug("allocation for value %s type string\n",_name.c_str());
	//TODO , 在堆栈上分配个变量
	llvm::IRBuilder<> builder(&ctx.llvmfunc->getEntryBlock(),
							  ctx.llvmfunc->getEntryBlock().begin());

	builder.SetInsertPoint(ctx.block);

	llvm::Value * newval = builder.CreateAlloca(this->llvm_type(ctx),0,_name);

	builder.CreateStore( qbc::getnull(), newval);
	return newval;
}

void StringExprTypeAST::destory(ASTContext ctx, llvm::Value* Ptr)
{
	debug("generate dealloca for string\n");

	llvm::IRBuilder<>	builder(ctx.block);

	llvm::Constant * func_free = qbc::getbuiltinprotype(ctx,"free");

	builder.CreateCall(func_free,builder.CreateLoad(Ptr));
}


llvm::Value* ConstNumberExprAST::getval(ASTContext ctx)
{
	return qbc::getconstlong(	this->v);
}

llvm::Value* ConstStringExprAST::getval(ASTContext ctx)
{
	if(val)
		return val;
	llvm::IRBuilder<>	builder(ctx.block);

	// cache the result
	return val = builder.CreateGlobalStringPtr( this->str );
}

DimAST* NamedExprAST::nameresolve(ASTContext ctx)
{
	// 首先查找变量的分配 FIXME 将来要支持结构体成员

	std::string varname =  this->ID->ID;

	debug("searching for var %s\n",varname.c_str());

	if(! ctx.codeblock ){
		debug("var %s not defined\n",varname.c_str());
		exit(1);
		return NULL;
	}

	std::map< std::string, DimAST* >::iterator dimast_iter = ctx.codeblock->symbols.find(varname);

	// 定义找到
	if(dimast_iter != ctx.codeblock->symbols.end()){

		debug("searching for var %s have result %p\n",varname.c_str(),dimast_iter->second);

		return dimast_iter->second;
	}
	ctx.codeblock = ctx.codeblock->parent;
	return nameresolve(ctx);
}

DimAST* CallExprAST::nameresolve(ASTContext ctx)
{
	std::string functionname = this->ID->ID;

	debug("searching for function %s\n",functionname.c_str());

	if(! ctx.codeblock ){

		fprintf(stderr,"function %s not defined, try to call any way, you will get ld undefined reference if function %s is not included in the linking command line\n",functionname.c_str(),functionname.c_str());

		return NULL;
	}

	std::map< std::string, FunctionDimAST* >::iterator dimast_iter = ctx.codeblock->functions.find(functionname);

	// 定义找到
	if(dimast_iter != ctx.codeblock->functions.end()){

		debug("searching for function %s have result %p\n",functionname.c_str(),dimast_iter->second);

		return dimast_iter->second;
	}
	ctx.codeblock = ctx.codeblock->parent;
	return nameresolve(ctx);
}


llvm::Value* CallExprAST::defaultprototype(ASTContext ctx, std::string functionname)
{
	//build default function type
	llvm::IRBuilder<>	builder(ctx.block);

	std::vector<llvm::Type*> no_args;

	return ctx.module->getOrInsertFunction(functionname,llvm::FunctionType::get(_numbertype.llvm_type(ctx), no_args,true));
}

llvm::Value* VariableExprAST::getval(ASTContext ctx)
{
 	llvm::IRBuilder<> builder(ctx.block);

	std::string desc = std::string("load local var ")+this->ID->ID;
	
	debug("%s\n",desc.c_str());
	
	return nameresolve(ctx)->getval(ctx);
}

// 获得变量的分配
llvm::Value* VariableExprAST::getptr(ASTContext ctx)
{
	return nameresolve(ctx)->getptr(ctx);
}

// so simple , right ?
llvm::Value* AssignmentExprAST::getval(ASTContext ctx)
{
	return this->type(ctx)->getop()->operator_assign(ctx,this->lval,this->rval)->getval(ctx);
}

llvm::Value* CallExprAST::getval(ASTContext ctx)
{
	llvm::IRBuilder<> builder(ctx.llvmfunc->getContext());
	builder.SetInsertPoint(ctx.block);

	// call functions TODO
    debug("sigfault herekkk?\n");
	llvm::Value * ret = NULL;

	//获得函数定义
	llvm::Value * llvmfunc = NULL;
	DimAST * dim = nameresolve(ctx);
	FunctionDimAST* fundim = dynamic_cast<FunctionDimAST*>(dim);
	
	if(fundim){ //有定义, 则直接调用, 无定义就 ... 呵呵
		llvmfunc = fundim->getptr(ctx);
	}else{
		llvmfunc = defaultprototype(ctx,this->ID->ID);
	}

	//构建参数列表
	std::vector<llvm::Value*> args;
	if(callargs && callargs->expression_list.size() )
	{
		BOOST_FOREACH( ExprASTPtr expr , callargs->expression_list)
		{
			debug("pusing args \n");
			args.push_back( expr->getval(ctx) );
		}
	}
	return builder.CreateCall(llvmfunc,args,this->ID->ID);
}

llvm::Value* CalcExprAST::getval(ASTContext ctx)
{
	
	BOOST_ASSERT(ctx.llvmfunc);
	//TODO, 生成计算表达式 !
	llvm::Value * LHS =	lval->getval(ctx);
	llvm::Value * RHS =	rval->getval(ctx);

	llvm::IRBuilder<> builder(ctx.llvmfunc->getContext());
	builder.SetInsertPoint(ctx.block);

	switch(this->op){
		case OPERATOR_ADD:
			if(!result)
				result = lval->type(ctx)->getop()->operator_add(ctx,lval,rval);
			return result->getval(ctx);
		case OPERATOR_SUB:
			if(!result)
				result = lval->type(ctx)->getop()->operator_sub(ctx,lval,rval);
			return result->getval(ctx);
		case OPERATOR_MUL:
			if(!result)
				result = lval->type(ctx)->getop()->operator_mul(ctx,lval,rval);
			return result->getval(ctx);
		case OPERATOR_DIV:
			if(!result)
				result = lval->type(ctx)->getop()->operator_div(ctx,lval,rval);
			return result->getval(ctx);
		case OPERATOR_GREATEREQUL:
		case OPERATOR_GREATER:
		case OPERATOR_LESS:
		case OPERATOR_LESSEQU:
			if(!result){
				result = lval->type(ctx)->getop()->operator_comp(ctx,op,lval,rval);
			}
			return result->getval(ctx);
		default:
			debug("operator not supported yet\n");
			exit(1);
	}
	return result->getval(ctx);
}

///////////////////////////////////////////////////////////////////
//////////////////// 构造函数们 ////////////////////////////////////
//////////////////////////////////////////////////////////////////
ExprTypeAST::ExprTypeAST(size_t size, const std::string __typename)
	:_size(size),_typename(__typename)
{
}

NumberExprTypeAST::NumberExprTypeAST() : ExprTypeAST(sizeof(long),"long")
{

}

StringExprTypeAST::StringExprTypeAST() :ExprTypeAST(sizeof(void*),"string")
{	
}

ConstStringExprAST::ConstStringExprAST(const std::string _str)
	:str(_str),val(0)
{
}

NamedExprAST::NamedExprAST(ReferenceAST* _ID)
	:ID(_ID)
{
}

VariableExprAST::VariableExprAST(ReferenceAST* ID)
	:NamedExprAST(ID)
{
}

AssignmentExprAST::AssignmentExprAST(NamedExprAST* l, ExprAST*r)
	:lval(l),rval(r)
{
}

CallOrArrayExprAST::CallOrArrayExprAST(ReferenceAST* _ID)
	:NamedExprAST(_ID)
{
}

CallExprAST::CallExprAST(ReferenceAST* ID, ExprListAST* exp_list)
	:CallOrArrayExprAST(ID),callargs(exp_list)
{
}

CalcExprAST::CalcExprAST(ExprAST* l, MathOperator _op, ExprAST* r)
	:lval(l),rval(r),op(_op)
{
	
}

TempExprAST::TempExprAST(ASTContext _ctx, llvm::Value* _val, ExprTypeASTPtr type) :ctx(_ctx),val(_val),_type(type) {}

TempNumberExprAST::TempNumberExprAST(ASTContext ctx,llvm::Value* numberresult)
	:TempExprAST(ctx,numberresult , numbertype)
{

}

TempStringExprAST::TempStringExprAST(ASTContext ctx,llvm::Value* result)
	:TempExprAST(ctx,result,  stringtype)
{
	
}

TempStringExprAST::~TempStringExprAST()
{
	llvm::IRBuilder<>	builder(ctx.block);
	llvm::Constant * func_free = qbc::getbuiltinprotype(ctx,"free");
	builder.CreateCall(func_free,this->val);
}
