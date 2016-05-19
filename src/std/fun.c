/*
 * Copyright (C)2005-2016 Haxe Foundation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <hl.h>

static void fun_var_args() {
	hl_error("Variable fun args was not cast to typed function");
}

HL_PRIM vclosure *hl_alloc_closure_void( hl_type *t, void *fvalue ) {
	vclosure *c = (vclosure*)hl_gc_alloc(sizeof(vclosure));
	c->t = t;
	c->fun = fvalue;
	c->hasValue = false;
	return c;
}

static hl_type *hl_get_closure_type( hl_type *t ) {
	hl_type_fun *ft = t->fun;
	if( ft->closure_type.kind != HFUN ) {
		if( ft->nargs == 0 ) hl_fatal("assert");
		ft->closure_type.kind = HFUN;
		ft->closure_type.p = &ft->closure;
		ft->closure.nargs = ft->nargs - 1;
		ft->closure.args = ft->closure.nargs ? ft->args + 1 : NULL;
		ft->closure.ret = ft->ret;
		ft->closure.parent = t;
	}
	return (hl_type*)&ft->closure_type;
}

HL_PRIM vclosure *hl_alloc_closure_ptr( hl_type *fullt, void *fvalue, void *v ) {
	vclosure *c = (vclosure*)hl_gc_alloc(sizeof(vclosure));
	c->t = hl_get_closure_type(fullt);
	c->fun = fvalue;
	c->hasValue = 1;
	c->value = v;
	return c;
}

HL_PRIM vdynamic *hl_no_closure( vdynamic *c ) {
	vclosure *cl = (vclosure*)c;
	if( !cl->hasValue ) return c;
	if( cl->hasValue == 2 )
		return hl_no_closure((vdynamic*)((vclosure_wrapper*)c)->wrappedFun);
	return (vdynamic*)hl_alloc_closure_void(cl->t->fun->parent,cl->fun);
}

HL_PRIM vdynamic* hl_get_closure_value( vdynamic *c ) {
	vclosure *cl = (vclosure*)c;
	if( cl->hasValue == 2 )
		return hl_get_closure_value((vdynamic*)((vclosure_wrapper*)c)->wrappedFun);
	if( cl->hasValue && cl->fun != fun_var_args )
		return hl_make_dyn(&cl->value, cl->t->fun->parent->fun->args[0]);
	return (vdynamic*)cl->value;
}

HL_PRIM bool hl_fun_compare( vdynamic *a, vdynamic *b ) {
	vclosure *ca, *cb;
	if( a == b )
		return true;
	if( !a || !b )
		return false;
	if( a->t->kind != b->t->kind || a->t->kind != HFUN )
		return false;
	ca = (vclosure*)a;
	cb = (vclosure*)b;
	if( ca->fun != cb->fun )
		return false;
	if( ca->hasValue && ca->value != cb->value )
		return false;
	return true;
}


// ------------ DYNAMIC CALLS

typedef void *(*fptr_static_call)(void *fun, hl_type *t, void **args, vdynamic *out);
typedef void *(*fptr_get_wrapper)(hl_type *t);

static fptr_static_call hlc_static_call = NULL;
static fptr_get_wrapper hlc_get_wrapper = NULL;

HL_PRIM void hlc_setup( void *c, void *w ) {
	hlc_static_call = (fptr_static_call)c;
	hlc_get_wrapper = (fptr_get_wrapper)w;
}

#define HL_MAX_ARGS 9

HL_PRIM vdynamic* hl_call_method( vdynamic *c, varray *args ) {
	vclosure *cl = (vclosure*)c;
	vdynamic **vargs = hl_aptr(args,vdynamic*);
	void *pargs[HL_MAX_ARGS];
	void *ret;
	union { double d; int i; float f; } tmp[HL_MAX_ARGS];
	hl_type *tret;
	vdynamic *dret;
	vdynamic out;
	int i;
	if( args->size > HL_MAX_ARGS )
		hl_error("Too many arguments");
	if( cl->hasValue ) {
		if( cl->fun == fun_var_args ) {
			cl = (vclosure*)cl->value;
			return cl->hasValue ? ((vdynamic* (*)(vdynamic*, varray*))cl->fun)(cl->value, args) : ((vdynamic* (*)(varray*))cl->fun)(args);
		}
		hl_error("Can't call closure with value");
	}
	if( args->size < cl->t->fun->nargs )
		hl_error_msg(USTR("Missing arguments : %d expected but %d passed"),cl->t->fun->nargs, args->size);
	for(i=0;i<cl->t->fun->nargs;i++) {
		vdynamic *v = vargs[i];
		hl_type *t = cl->t->fun->args[i];
		void *p;
		if( v == NULL ) {
			if( hl_is_ptr(t) )
				p = NULL;
			else {
				tmp[i].d = 0;
				p = &tmp[i].d;
			}
		} else switch( t->kind ) {
		case HBOOL:
		case HI8:
		case HI16:
		case HI32:
			tmp[i].i = hl_dyn_casti(vargs + i, &hlt_dyn,t);
			p = &tmp[i].i;
			break;
		case HF32:
			tmp[i].f = hl_dyn_castf(vargs + i, &hlt_dyn);
			p = &tmp[i].f;
			break;
		case HF64:
			tmp[i].d = hl_dyn_castd(vargs + i, &hlt_dyn);
			p = &tmp[i].d;
			break;
		default:
			p = hl_dyn_castp(vargs + i,&hlt_dyn,t);
			break;
		}
		pargs[i] = p;
	}
	ret = hlc_static_call(cl->fun,cl->t,pargs,&out);
	tret = cl->t->fun->ret;
	if( !hl_is_ptr(tret) ) {
		vdynamic *r = hl_alloc_dynamic(tret);
		r->t = tret;
		r->v.d = out.v.d; // copy
		return r;
	}
	if( ret == NULL || hl_is_dynamic(tret) )
		return (vdynamic*)ret;
	dret = hl_alloc_dynamic(tret);
	dret->v.ptr = ret;
	return dret;
}

HL_PRIM void *hl_wrapper_call( void *_c, void **args, vdynamic *ret ) {
	vclosure_wrapper *c = (vclosure_wrapper*)_c;
	hl_type_fun *tfun = c->cl.t->fun;
	union { double d; int i; float f; } tmp[HL_MAX_ARGS];
	void *vargs[HL_MAX_ARGS+1];
	vdynamic out;
	vclosure *w = c->wrappedFun;
	int i;
	int p = 0;
	void *pret, *aret;
	if( ret == NULL )
		ret = &out;
	if( w->fun == fun_var_args ) {
		varray *a;
		w = (vclosure*)w->value; // the real callback
		a = hl_alloc_array(&hlt_dyn,tfun->nargs);
		for(i=0;i<tfun->nargs;i++) {
			hl_type *t = tfun->args[i];
			void *v = hl_is_ptr(t) ? args + i : args[i];
			hl_aptr(a,void*)[i] = hl_make_dyn(v,t);
		}
		if( w->hasValue )
			vargs[p++] = (vdynamic*)w->value;
		vargs[p++] = (vdynamic*)a;
	} else {
		if( w->hasValue )
			vargs[p++] = (vdynamic*)w->value;
		for(i=0;i<w->t->fun->nargs;i++) {
			hl_type *t = tfun->args[i];
			hl_type *to = w->t->fun->args[i];
			void *v = hl_is_ptr(t) ? args + i : args[i];
			switch( to->kind ) {
			case HI8:
			case HI16:
			case HI32:
			case HBOOL:
				tmp[i].i = hl_dyn_casti(v,t,to);
				v = &tmp[i].i;
				break;
			case HF32:
				tmp[i].f = hl_dyn_castf(v,t);
				v = &tmp[i].f;
				break;
			case HF64:
				tmp[i].d = hl_dyn_castd(v,t);
				v = &tmp[i].d;
				break;
			default:
				v = hl_dyn_castp(v,t,to);
				break;
			}
			vargs[p++] = v;
		}
	}
	pret = hlc_static_call(w->fun,w->hasValue ? w->t->fun->parent : w->t,vargs,ret);
	aret = hl_is_ptr(w->t->fun->ret) ? &pret : pret;
	if( aret == NULL ) aret = &pret;
	switch( tfun->ret->kind ) {
	case HI8:
	case HI16:
	case HI32:
		ret->v.i = hl_dyn_casti(aret,w->t->fun->ret,tfun->ret);
		break;
	case HF32:
		ret->v.f = hl_dyn_castf(aret,w->t->fun->ret);
		break;
	case HF64:
		ret->v.d = hl_dyn_castd(aret,w->t->fun->ret);
		break;
	default:
		pret = hl_dyn_castp(aret,w->t->fun->ret,tfun->ret);
		break;
	}
	return pret;
}

HL_PRIM vclosure *hl_make_fun_wrapper( vclosure *v, hl_type *to ) {
	vclosure_wrapper *c;
	void *wrap = hlc_get_wrapper(to);
	if( wrap == NULL ) return NULL;
	if( v->fun != fun_var_args && v->t->fun->nargs != to->fun->nargs )
		return NULL;
	c = (vclosure_wrapper*)hl_gc_alloc(sizeof(vclosure_wrapper));
	c->cl.t = to;
	c->cl.fun = wrap;
	c->cl.hasValue = 2;
	c->cl.value = c;
	c->wrappedFun = v; 
	return (vclosure*)c;
}

static hl_type hlt_var_args = { HFUN };
static hl_type_fun hlt_var_fun = { NULL, &hlt_void, -1, &hlt_var_args, { HFUN, NULL }, { NULL, &hlt_void, -1, &hlt_var_args} };

HL_PRIM vdynamic *hl_make_var_args( vclosure *c ) {
	hlt_var_args.fun = &hlt_var_fun;
	hlt_var_fun.closure_type.p = &hlt_var_fun;
	return (vdynamic*)hl_alloc_closure_ptr(&hlt_var_args,fun_var_args,c);
}
