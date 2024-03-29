/***************************************************************/
/*                                                             */
/*  EXPR.C                                                     */
/*                                                             */
/*  This file contains routines to parse and evaluate          */
/*  expressions.                                               */
/*                                                             */
/*  Copyright 1992-2022 by Dianne Skoll                        */
/*                                                             */
/***************************************************************/

#include "config.h"
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>

#include <stdlib.h>

#include "err.h"
#include "types.h"
#include "expr.h"
#include "protos.h"
#include "globals.h"

#define ISID(c) (isalnum(c) || (c) == '_')
#define EQ 0
#define GT 1
#define LT 2
#define GE 3
#define LE 4
#define NE 5

extern int NumFuncs;

static int Multiply(void), Divide(void), Mod(void), Add(void),
    Subtract(void), GreaterThan(void), LessThan(void),
    EqualTo(void), NotEqual(void), LessOrEqual(void),
    GreaterOrEqual(void), LogAND(void), LogOR(void),
    UnMinus(void), LogNot(void),
    Compare(int);

static int MakeValue (char const *s, Value *v, Var *locals, ParsePtr p);

/* Binary operators - all left-associative */

/* Operator precedence:
 *   Highest:    Unary -    Unary !
 *               /   %
 *               +   -
 *               <   <=   >   >=
 *               ==   !=
 *               &&
 *    Lowest:    ||
 *
 */

/* Make SURE they are sorted lexically... this may die on an EBCDIC
   system... */

Operator BinOp[] = {
    { "!=", 15, BIN_OP, NotEqual },
    { "%",  20, BIN_OP, Mod },
    { "&&", 14, BIN_OP, LogAND },
    { "*",  20, BIN_OP, Multiply },
    { "+",  18, BIN_OP, Add },
    { "-",  18, BIN_OP, Subtract },
    { "/",  20, BIN_OP, Divide },
    { "<",  16, BIN_OP, LessThan },
    { "<=", 16, BIN_OP, LessOrEqual },
    { "==", 15, BIN_OP, EqualTo },
    { ">",  16, BIN_OP, GreaterThan },
    { ">=", 16, BIN_OP, GreaterOrEqual },
    { "||", 12, BIN_OP, LogOR },
};
#define NUM_BIN_OPS (sizeof(BinOp) / sizeof(Operator))

/* These ones must be sorted too. */
Operator UnOp[] = {
    { "!",  22, UN_OP, LogNot },
    { "-",  22, UN_OP, UnMinus },
};
#define NUM_UN_OPS (sizeof(UnOp) / sizeof(Operator))

extern BuiltinFunc Func[];

static Operator OpStack[OP_STACK_SIZE];
static int      OpStackPtr = 0;

/* ValStack can't be static - needed by funcs.c */
       Value    ValStack[VAL_STACK_SIZE];
       int      ValStackPtr = 0;

/***************************************************************/
/*                                                             */
/*  DebugPerform                                               */
/*                                                             */
/*  Execute an operator or function with debugging.            */
/*                                                             */
/***************************************************************/
static int DebugPerform(Operator *op)
{
    int r;

    if (op->type == UN_OP) {
	fprintf(ErrFp, "%s ", op->name);
	PrintValue(&ValStack[ValStackPtr-1], ErrFp);
    } else { /* Must be binary operator */
	PrintValue(&ValStack[ValStackPtr-2], ErrFp);
	fprintf(ErrFp, " %s ", op->name);
	PrintValue(&ValStack[ValStackPtr-1], ErrFp);
    }

    r = (op->func)();
    fprintf(ErrFp, " => ");
    if (!r) {
	PrintValue(&ValStack[ValStackPtr-1], ErrFp);
	putc('\n', ErrFp);
    } else {
	fprintf(ErrFp, "%s\n", ErrMsg[r]);
    }
    return r;
}

/***************************************************************/
/*                                                             */
/*  CleanStack                                                 */
/*                                                             */
/*  Clean the stack after an error occurs.                     */
/*                                                             */
/***************************************************************/
static void CleanStack(int old_op_stack_ptr, int old_val_stack_ptr)
{
    int i;

    for (i=old_val_stack_ptr; i<ValStackPtr; i++) DestroyValue(ValStack[i]);
    ValStackPtr = old_val_stack_ptr;
    OpStackPtr = old_op_stack_ptr;
}

/***************************************************************/
/*                                                             */
/*  PeekChar - peek ahead to next char.                        */
/*                                                             */
/***************************************************************/
static char PeekChar(char const **s)
{
    char const *t = *s;
    while (*t && isempty(*t)) t++;
    return *t;
}

/***************************************************************/
/*                                                             */
/*  ParseExprToken                                             */
/*                                                             */
/*  Read a token.                                              */
/*                                                             */
/***************************************************************/
static int ParseExprToken(DynamicBuffer *buf, char const **in)
{

    char c;

    DBufFree(buf);
/* Skip white space */
    while (**in && isempty(**in)) (*in)++;

    if (!**in) return OK;

    c = *(*in)++;
    if (DBufPutc(buf, c) != OK) {
	DBufFree(buf);
	return E_NO_MEM;
    }

    switch(c) {
    case COMMA:
    case END_OF_EXPR:
    case '+':
    case '-':
    case '*':
    case '/':
    case '(':
    case ')':
    case '%': return OK;

    case '&':
    case '|':
    case '=':
	if (**in == c) {
	    if (DBufPutc(buf, c) != OK) {
		DBufFree(buf);
		return E_NO_MEM;
	    }
	    (*in)++;
	}
	return OK;

    case '!':
    case '>':
    case '<':
	if (**in == '=') {
	    if (DBufPutc(buf, '=') != OK) {
		DBufFree(buf);
		return E_NO_MEM;
	    }
	    (*in)++;
	}
	return OK;
    }


    /* Handle the parsing of quoted strings */
    if (c == '\"') {
	if (!**in) return E_MISS_QUOTE;
	while (**in) {
	    /* Allow backslash-escapes */
	    if (**in == '\\') {
		int r;
		(*in)++;
		if (!**in) {
		    DBufFree(buf);
		    return E_MISS_QUOTE;
		}
		switch(**in) {
		case 'a':
		    r = DBufPutc(buf, '\a');
		    break;
		case 'b':
		    r = DBufPutc(buf, '\b');
		    break;
		case 'f':
		    r = DBufPutc(buf, '\f');
		    break;
		case 'n':
		    r = DBufPutc(buf, '\n');
		    break;
		case 'r':
		    r = DBufPutc(buf, '\r');
		    break;
		case 't':
		    r = DBufPutc(buf, '\t');
		    break;
		case 'v':
		    r = DBufPutc(buf, '\v');
		    break;
		default:
		    r = DBufPutc(buf, **in);
		}
		(*in)++;
		if (r != OK) {
		    DBufFree(buf);
		    return E_NO_MEM;
		}
                if (!**in) {
                    DBufFree(buf);
                    return E_MISS_QUOTE;
                }
		continue;
	    }
	    c = *(*in)++;
	    if (DBufPutc(buf, c) != OK) {
		DBufFree(buf);
		return E_NO_MEM;
	    }
	    if (c == '\"') break;
	}
	if (c == '\"') return OK;
	DBufFree(buf);
	return E_MISS_QUOTE;
    }

    /* Dates can be specified with single-quotes */
    if (c == '\'') {
	if (!**in) return E_MISS_QUOTE;
	while (**in) {
	    c = *(*in)++;
	    if (DBufPutc(buf, c) != OK) {
		DBufFree(buf);
		return E_NO_MEM;
	    }
	    if (c == '\'') break;
	}
	if (c == '\'') return OK;
	DBufFree(buf);
	return E_MISS_QUOTE;
    }

    if (!ISID(c) && c != '$') {
	Eprint("%s `%c'", ErrMsg[E_ILLEGAL_CHAR], c);
	return E_ILLEGAL_CHAR;
    }

    /* Parse a constant, variable name or function */
    while (ISID(**in) || **in == ':' || **in == '.' || **in == TimeSep) {
	if (DBufPutc(buf, **in) != OK) {
	    DBufFree(buf);
	    return E_NO_MEM;
	}
	(*in)++;
    }
    /* Chew up any remaining white space */
    while (**in && isempty(**in)) (*in)++;

    /* Peek ahead - is it '('?  Then we have a function call */
    if (**in == '(') {
	if (DBufPutc(buf, '(') != OK) {
	    DBufFree(buf);
	    return E_NO_MEM;
	}
	(*in)++;
    }
    return OK;
}

/***************************************************************/
/*                                                             */
/*  EvalExpr                                                   */
/*  Evaluate an expression.  Return 0 if OK, non-zero if error */
/*  Put the result into value pointed to by v.                 */
/*                                                             */
/***************************************************************/
int EvalExpr(char const **e, Value *v, ParsePtr p)
{
    int r;

    int old_op_stack_ptr = OpStackPtr;
    int old_val_stack_ptr = ValStackPtr;

    r = Evaluate(e, NULL, p);

    /* Put last character parsed back onto input stream */
    if (DBufLen(&ExprBuf)) (*e)--;
    DBufFree(&ExprBuf);

    if (r) {
	CleanStack(old_op_stack_ptr, old_val_stack_ptr);
        /* fprintf(stderr, "O=%d V=%d\n", OpStackPtr, ValStackPtr); */
	return r;
    }
    OpStackPtr = old_op_stack_ptr;
    ValStackPtr = old_val_stack_ptr;
    *v = ValStack[old_val_stack_ptr];
    ValStack[old_val_stack_ptr].type = ERR_TYPE;
    /* fprintf(stderr, "O=%d V=%d\n", OpStackPtr, ValStackPtr); */
    return r;
}

/* Evaluate - do the actual work of evaluation. */
int Evaluate(char const **s, Var *locals, ParsePtr p)
{
    int OpBase, ValBase;
    int r;
    Operator *o;
    BuiltinFunc *f;
    int args; /* Number of function arguments */
    Operator op, op2;
    Value va;
    char const *ufname = NULL; /* Stop GCC from complaining about use of uninit var */

    OpBase = OpStackPtr;
    ValBase = ValStackPtr;

    while(1) {
/* Looking for a value.  Accept: value, unary op, func. call or left paren */
	r = ParseExprToken(&ExprBuf, s);
	if (r) return r;
	if (!DBufLen(&ExprBuf)) {
	    DBufFree(&ExprBuf);
	    return E_EOLN;
	}

	if (*DBufValue(&ExprBuf) == '(') { /* Parenthesized expression */
	    DBufFree(&ExprBuf);
	    r = Evaluate(s, locals, p);  /* Leaves the last parsed token in ExprBuf */
	    if (r) return r;
	    r = OK;
	    if (*DBufValue(&ExprBuf) != ')') {
		DBufFree(&ExprBuf);
		return E_MISS_RIGHT_PAREN;
	    }
	    if (r) return r;
	} else if (*DBufValue(&ExprBuf) == '+') {
	    continue; /* Ignore unary + */
	}
	else if (*(DBufValue(&ExprBuf) + DBufLen(&ExprBuf) -1) == '(') { /* Function Call */
	    *(DBufValue(&ExprBuf) + DBufLen(&ExprBuf) - 1) = 0;
	    f = FindFunc(DBufValue(&ExprBuf), Func, NumFuncs);
	    if (!f) {
		ufname = StrDup(DBufValue(&ExprBuf));
		DBufFree(&ExprBuf);
		if (!ufname) return E_NO_MEM;
	    } else {
		DBufFree(&ExprBuf);
	    }
	    args = 0;
	    if (PeekChar(s) == ')') { /* Function has no arguments */
		if (f) {
		    if (!f->is_constant && (p != NULL)) p->nonconst_expr = 1;
		    r = CallFunc(f, 0);
		} else {
		    r = CallUserFunc(ufname, 0, p);
		    free((char *) ufname);
		}
		if (r) return r;
		r = ParseExprToken(&ExprBuf, s); /* Guaranteed to be right paren. */
		if (r) return r;
	    } else { /* Function has some arguments */
		while(1) {
		    args++;
		    r = Evaluate(s, locals, p);
		    if (r) {
			if (!f) free((char *) ufname);
			return r;
		    }
		    if (*DBufValue(&ExprBuf) == ')') break;
		    else if (*DBufValue(&ExprBuf) != ',') {
			if (!f) free((char *) ufname);
			Eprint("%s: `%c'", ErrMsg[E_EXPECT_COMMA],
			       *DBufValue(&ExprBuf));
			DBufFree(&ExprBuf);
			return E_EXPECT_COMMA;
		    }
		}
		if (f) {
		    if (!f->is_constant && (p != NULL)) p->nonconst_expr = 1;
		    r = CallFunc(f, args);
		} else {
		    r = CallUserFunc(ufname, args, p);
		    free((char *) ufname);
		}
		DBufFree(&ExprBuf);
		if (r) return r;
	    }
	} else { /* Unary operator */
	    o = FindOperator(DBufValue(&ExprBuf), UnOp, NUM_UN_OPS);
	    if (o) {
		DBufFree(&ExprBuf);
		PushOpStack(*o);
		continue;  /* Still looking for an atomic vlue */
	    } else if (!ISID(*DBufValue(&ExprBuf)) &&
		       *DBufValue(&ExprBuf) != '$' &&
		       *DBufValue(&ExprBuf) != '"' &&
		       *DBufValue(&ExprBuf) != '\'') {
		Eprint("%s `%c'", ErrMsg[E_ILLEGAL_CHAR],
		       *DBufValue(&ExprBuf));
		DBufFree(&ExprBuf);
		return E_ILLEGAL_CHAR;
	    } else { /* Must be a literal value */
		r = MakeValue(DBufValue(&ExprBuf), &va, locals, p);
		DBufFree(&ExprBuf);
		if (r) return r;
		PushValStack(va);
	    }
	}
/* OK, we've got a literal value; now, we're looking for the end of the
   expression, or a binary operator. */
	r = ParseExprToken(&ExprBuf, s);
	if (r) return r;
	if (*DBufValue(&ExprBuf) == 0 ||
	    *DBufValue(&ExprBuf) == ',' ||
	    *DBufValue(&ExprBuf) == ']' ||
	    *DBufValue(&ExprBuf) == ')') {
	    /* We've hit the end of the expression.  Pop off and evaluate until
	       OpStackPtr = OpBase and ValStackPtr = ValBase+1 */
	    while (OpStackPtr > OpBase) {
		PopOpStack(op);
		if (DebugFlag & DB_PRTEXPR)
		    r=DebugPerform(&op);
		else
		    r=(op.func)();
		if (r) {
		    DBufFree(&ExprBuf);
		    Eprint("`%s': %s", op.name, ErrMsg[r]);
		    return r;
		}
	    }
	    if (ValStackPtr != ValBase+1) {
		DBufFree(&ExprBuf);
		return E_STACK_ERR;
	    }
	    return OK;
	}
	/* Must be a binary operator */
	o = FindOperator(DBufValue(&ExprBuf), BinOp, NUM_BIN_OPS);
	DBufFree(&ExprBuf);
	if (!o) return E_EXPECTING_BINOP;

	/* While operators of higher or equal precedence are on the stack,
	   pop them off and evaluate */
	while (OpStackPtr > OpBase && OpStack[OpStackPtr-1].prec >= o->prec) {
	    PopOpStack(op2);
	    if (r) return r;
	    if (DebugFlag & DB_PRTEXPR)
		r=DebugPerform(&op2);
	    else
		r=(op2.func)();
	    if (r) {
		Eprint("`%s': %s", op2.name, ErrMsg[r]);
		return r;
	    }
	}
	PushOpStack(*o);
    }
}

/***************************************************************/
/*                                                             */
/*  MakeValue                                                  */
/*  Generate a literal value.  It's either a string, a number, */
/*  a date or the value of a symbol.                           */
/*                                                             */
/***************************************************************/
static int MakeValue(char const *s, Value *v, Var *locals, ParsePtr p)
{
    int len;
    int h, m, r;
    int ampm = 0;
    int prev_val;

    if (*s == '\"') { /* It's a literal string "*/
	len = strlen(s)-1;
	v->type = STR_TYPE;
	v->v.str = malloc(len);
	if (! v->v.str) {
	    v->type = ERR_TYPE;
	    return E_NO_MEM;
	}
	strncpy(v->v.str, s+1, len-1);
	*(v->v.str+len-1) = 0;
	return OK;
    } else if (*s == '\'') { /* It's a literal date */
	s++;
	if ((r=ParseLiteralDate(&s, &h, &m))) return r;
	if (*s != '\'') return E_BAD_DATE;
	if (m == NO_TIME) {
	    v->type = DATE_TYPE;
	    v->v.val = h;
	} else {
	    v->type = DATETIME_TYPE;
	    v->v.val = (h * MINUTES_PER_DAY) + m;
	}
	return OK;
    } else if (isdigit(*s)) { /* It's a number - use len to hold it.*/
	len = 0;
        prev_val = 0;
	while (*s && isdigit(*s)) {
	    len *= 10;
	    len += (*s++ - '0');
            if (len < prev_val) {
                /* We overflowed */
                return E_2HIGH;
            }
            prev_val = len;
	}
	if (*s == ':' || *s == '.' || *s == TimeSep) { /* Must be a literal time */
	    s++;
	    if (!isdigit(*s)) return E_BAD_TIME;
	    h = len;
	    m = 0;
	    while (isdigit(*s)) {
		m *= 10;
		m += *s - '0';
		s++;
	    }
	    /* Check for p[m] or a[m] */
	    if (*s == 'A' || *s == 'a' || *s == 'P' || *s == 'p') {
		ampm = tolower(*s);
		s++;
		if (*s == 'm' || *s == 'M') {
		    s++;
		}
	    }
	    if (*s || h>23 || m>59) return E_BAD_TIME;
	    if (ampm) {
		if (h < 1 || h > 12) return E_BAD_TIME;
		if (ampm == 'a') {
		    if (h == 12) {
			h = 0;
		    }
		} else if (ampm == 'p') {
		    if (h < 12) {
			h += 12;
		    }
		}
	    }
	    v->type = TIME_TYPE;
	    v->v.val = h*60 + m;
	    return OK;
	}
	/* Not a time - must be a number */
	if (*s) return E_BAD_NUMBER;
	v->type = INT_TYPE;
	v->v.val = len;
	return OK;
    } else if (*s == '$') { /* A system variable */
	if (p) p->nonconst_expr = 1;
	if (DebugFlag & DB_PRTEXPR)
	    fprintf(ErrFp, "%s => ", s);
	r = GetSysVar(s+1, v);

	if (! (DebugFlag & DB_PRTEXPR)) return r;
	if (r == OK) {
	    PrintValue(v, ErrFp);
	    putc('\n', ErrFp);
	}
	return r;
    } else { /* Must be a symbol */
	if (DebugFlag & DB_PRTEXPR)
	    fprintf(ErrFp, "%s => ", s);
    }
    r = GetVarValue(s, v, locals, p);
    if (! (DebugFlag & DB_PRTEXPR)) return r;
    if (r == OK) {
	PrintValue(v, ErrFp);
	putc('\n', ErrFp);
    }
    return r;
}

/***************************************************************/
/*                                                             */
/*  DoCoerce - actually coerce a value to the specified type.  */
/*                                                             */
/***************************************************************/
int DoCoerce(char type, Value *v)
{
    int h, d, m, y, i, k;
    char const *s;

    char coerce_buf[128];

    /* Do nothing if value is already the right type */
    if (type == v->type) return OK;

    switch(type) {
    case DATETIME_TYPE:
	switch(v->type) {
	case INT_TYPE:
	    v->type = DATETIME_TYPE;
	    return OK;
	case DATE_TYPE:
	    v->type = DATETIME_TYPE;
	    v->v.val *= MINUTES_PER_DAY;
	    return OK;
	case STR_TYPE:
	    s = v->v.str;
	    if (ParseLiteralDate(&s, &i, &m)) return E_CANT_COERCE;
	    if (*s) return E_CANT_COERCE;
	    v->type = DATETIME_TYPE;
	    free(v->v.str);
	    if (m == NO_TIME) m = 0;
	    v->v.val = i * MINUTES_PER_DAY + m;
	    return OK;
	default:
	    return E_CANT_COERCE;
	}
    case STR_TYPE:
	switch(v->type) {
	case INT_TYPE: sprintf(coerce_buf, "%d", v->v.val); break;
	case TIME_TYPE: sprintf(coerce_buf, "%02d%c%02d", v->v.val / 60,
			       TimeSep, v->v.val % 60);
	break;
	case DATE_TYPE: FromJulian(v->v.val, &y, &m, &d);
	    sprintf(coerce_buf, "%04d%c%02d%c%02d",
		    y, DateSep, m+1, DateSep, d);
	    break;
	case DATETIME_TYPE:
	    i = v->v.val / MINUTES_PER_DAY;
	    FromJulian(i, &y, &m, &d);
	    k = v->v.val % MINUTES_PER_DAY;
	    h = k / 60;
	    i = k % 60;
	    sprintf(coerce_buf, "%04d%c%02d%c%02d%c%02d%c%02d",
		    y, DateSep, m+1, DateSep, d, DateTimeSep, h, TimeSep, i);
	    break;
	default: return E_CANT_COERCE;
	}
	v->type = STR_TYPE;
	v->v.str = StrDup(coerce_buf);
	if (!v->v.str) {
	    v->type = ERR_TYPE;
	    return E_NO_MEM;
	}
	return OK;

    case INT_TYPE:
	i = 0;
	m = 1;
	switch(v->type) {
	case STR_TYPE:
	    s = v->v.str;
	    if (*s == '-') {
		m = -1;
		s++;
	    }
	    while(*s && isdigit(*s)) {
		i *= 10;
		i += (*s++) - '0';
	    }
	    if (*s) {
		free (v->v.str);
		v->type = ERR_TYPE;
		return E_CANT_COERCE;
	    }
	    free(v->v.str);
	    v->type = INT_TYPE;
	    v->v.val = i * m;
	    return OK;

	case DATE_TYPE:
	case TIME_TYPE:
	case DATETIME_TYPE:
	    v->type = INT_TYPE;
	    return OK;

	default: return E_CANT_COERCE;
	}

    case DATE_TYPE:
	switch(v->type) {
	case INT_TYPE:
	    if(v->v.val >= 0) {
		v->type = DATE_TYPE;
		return OK;
	    } else return E_2LOW;

	case STR_TYPE:
	    s = v->v.str;
	    if (ParseLiteralDate(&s, &i, &m)) return E_CANT_COERCE;
	    if (*s) return E_CANT_COERCE;
	    v->type = DATE_TYPE;
	    free(v->v.str);
	    v->v.val = i;
	    return OK;

	case DATETIME_TYPE:
	    v->type = DATE_TYPE;
	    v->v.val /= MINUTES_PER_DAY;
	    return OK;

	default: return E_CANT_COERCE;
	}

    case TIME_TYPE:
	switch(v->type) {
	case INT_TYPE:
	case DATETIME_TYPE:
	    v->type = TIME_TYPE;
	    v->v.val %= MINUTES_PER_DAY;
	    if (v->v.val < 0) v->v.val += MINUTES_PER_DAY;
	    return OK;

	case STR_TYPE:
	    s = v->v.str;
	    if (ParseLiteralTime(&s, &i)) return E_CANT_COERCE;
	    if (*s) return E_CANT_COERCE;
	    v->type = TIME_TYPE;
	    free(v->v.str);
	    v->v.val = i;
	    return OK;

	default: return E_CANT_COERCE;
	}
    default: return E_CANT_COERCE;
    }
}

/***************************************************************/
/*                                                             */
/*  Add                                                        */
/*                                                             */
/*  Perform addition.                                          */
/*                                                             */
/***************************************************************/
static int Add(void)
{
    Value v1, v2, v3;
    int r;

    size_t l1, l2;

    PopValStack(v2);
    if ( (r = FnPopValStack(&v1)) ) {
	DestroyValue(v2);
	return r;
    }

/* If both are ints, just add 'em */
    if (v2.type == INT_TYPE && v1.type == INT_TYPE) {
        /* Check for overflow */
        if (_private_add_overflow(v1.v.val, v2.v.val)) {
            return E_2HIGH;
        }
	v1.v.val += v2.v.val;
	PushValStack(v1);
	return OK;
    }

/* If it's a date plus an int, add 'em */
    if ((v1.type == DATE_TYPE && v2.type == INT_TYPE) ||
	(v1.type == INT_TYPE && v2.type == DATE_TYPE)) {
        if (_private_add_overflow(v1.v.val, v2.v.val)) return E_DATE_OVER;
	v1.v.val += v2.v.val;
	if (v1.v.val < 0) return E_DATE_OVER;
	v1.type = DATE_TYPE;
	PushValStack(v1);
	return OK;
    }

/* If it's a datetime plus an int or a time, add 'em */
    if ((v1.type == DATETIME_TYPE && (v2.type == INT_TYPE || v2.type == TIME_TYPE)) ||
	((v1.type == INT_TYPE || v1.type == TIME_TYPE) && v2.type == DATETIME_TYPE)) {
        if (_private_add_overflow(v1.v.val, v2.v.val)) return E_DATE_OVER;
	v1.v.val += v2.v.val;
	if (v1.v.val < 0) return E_DATE_OVER;
	v1.type = DATETIME_TYPE;
	PushValStack(v1);
	return OK;
    }

/* If it's a time plus an int or a time plus a time,
   add 'em mod MINUTES_PER_DAY */
    if ((v1.type == TIME_TYPE && v2.type == INT_TYPE) ||
	(v1.type == INT_TYPE && v2.type == TIME_TYPE) ||
	(v1.type == TIME_TYPE && v2.type == TIME_TYPE)) {
        if (_private_add_overflow(v1.v.val, v2.v.val)) return E_DATE_OVER;
	v1.v.val += v2.v.val;
	v1.v.val = v1.v.val % MINUTES_PER_DAY;
	if (v1.v.val < 0) v1.v.val += MINUTES_PER_DAY;
	v1.type = TIME_TYPE;
	PushValStack(v1);
	return OK;
    }

/* If either is a string, coerce them both to strings and concatenate */
    if (v1.type == STR_TYPE || v2.type == STR_TYPE) {
	if ( (r = DoCoerce(STR_TYPE, &v1)) ) {
	    DestroyValue(v1); DestroyValue(v2);
	    return r;
	}
	if ( (r = DoCoerce(STR_TYPE, &v2)) ) {
	    DestroyValue(v1); DestroyValue(v2);
	    return r;
	}
	v3.type = STR_TYPE;
	l1 = strlen(v1.v.str);
	l2 = strlen(v2.v.str);
	if (MaxStringLen > 0 && (l1 + l2 > (size_t) MaxStringLen)) {
	    DestroyValue(v1); DestroyValue(v2);
	    return E_STRING_TOO_LONG;
	}
	v3.v.str = malloc(l1 + l2 + 1);
	if (!v3.v.str) {
	    DestroyValue(v1); DestroyValue(v2);
	    return E_NO_MEM;
	}
	strcpy(v3.v.str, v1.v.str);
	strcpy(v3.v.str+l1, v2.v.str);
	DestroyValue(v1); DestroyValue(v2);
	PushValStack(v3);
	return OK;
    }

    /* Don't handle other types yet */
    return E_BAD_TYPE;
}

/***************************************************************/
/*                                                             */
/*  Subtract                                                   */
/*                                                             */
/*  Perform subtraction.                                       */
/*                                                             */
/***************************************************************/
static int Subtract(void)
{
    Value v1, v2;
    int r;

    PopValStack(v2);
    if ( (r = FnPopValStack(&v1)) ) {
	DestroyValue(v2);
	return r;
    }

    /* If they're both INTs, do subtraction */
    if (v1.type == INT_TYPE && v2.type == INT_TYPE) {
        if (_private_sub_overflow(v1.v.val, v2.v.val)) return E_2HIGH;
	v1.v.val -= v2.v.val;
	PushValStack(v1);
	return OK;
    }

    /* If it's a date minus an int, do subtraction, checking for underflow */
    if (v1.type == DATE_TYPE && v2.type == INT_TYPE) {
        if (_private_sub_overflow(v1.v.val, v2.v.val)) return E_DATE_OVER;
	v1.v.val -= v2.v.val;
	if (v1.v.val < 0) return E_DATE_OVER;
	PushValStack(v1);
	return OK;
    }

    /* If it's a datetime minus an int or a time, do subtraction,
     * checking for underflow */
    if (v1.type == DATETIME_TYPE && (v2.type == INT_TYPE || v2.type == TIME_TYPE)) {
        if (_private_sub_overflow(v1.v.val, v2.v.val)) return E_DATE_OVER;
	v1.v.val -= v2.v.val;
	if (v1.v.val < 0) return E_DATE_OVER;
	PushValStack(v1);
	return OK;
    }

    /* If it's a time minus an int, do subtraction mod MINUTES_PER_DAY */
    if (v1.type == TIME_TYPE && v2.type == INT_TYPE) {
	v1.v.val = (v1.v.val - v2.v.val) % MINUTES_PER_DAY;
	if (v1.v.val < 0) v1.v.val += MINUTES_PER_DAY;
	PushValStack(v1);
	return OK;
    }

    /* If it's a time minus a time or a date minus a date, do it */
    if ((v1.type == TIME_TYPE && v2.type == TIME_TYPE) ||
	(v1.type == DATETIME_TYPE && v2.type == DATETIME_TYPE) ||
	(v1.type == DATE_TYPE && v2.type == DATE_TYPE)) {
        if (_private_sub_overflow(v1.v.val, v2.v.val)) return E_DATE_OVER;
	v1.v.val -= v2.v.val;
	v1.type = INT_TYPE;
	PushValStack(v1);
	return OK;
    }

    /* Must be types illegal for subtraction */
    DestroyValue(v1); DestroyValue(v2);
    return E_BAD_TYPE;
}

/***************************************************************/
/*                                                             */
/*  Multiply                                                   */
/*                                                             */
/*  Perform multiplication.                                    */
/*                                                             */
/***************************************************************/
static int Multiply(void)
{
    Value v1, v2;
    int r;

    PopValStack(v2);
    if ( (r = FnPopValStack(&v1)) ) {
	DestroyValue(v2);
	return r;
    }

    if (v1.type == INT_TYPE && v2.type == INT_TYPE) {
        /* Prevent floating-point exception */
        if ((v2.v.val == -1 && v1.v.val == INT_MIN) ||
            (v1.v.val == -1 && v2.v.val == INT_MIN)) {
            return E_2HIGH;
        }
        if (_private_mul_overflow(v1.v.val, v2.v.val)) return E_2HIGH;
	v1.v.val *= v2.v.val;
	PushValStack(v1);
	return OK;
    }
    DestroyValue(v1); DestroyValue(v2);
    return E_BAD_TYPE;
}

/***************************************************************/
/*                                                             */
/*  Divide                                                     */
/*                                                             */
/*  Perform division.                                          */
/*                                                             */
/***************************************************************/
static int Divide(void)
{
    Value v1, v2;
    int r;

    PopValStack(v2);
    if ( (r = FnPopValStack(&v1)) ) {
	DestroyValue(v2);
	return r;
    }

    if (v1.type == INT_TYPE && v2.type == INT_TYPE) {
	if (v2.v.val == 0) return E_DIV_ZERO;
        /* This is the only way it can overflow */
        if (v2.v.val == -1 && v1.v.val == INT_MIN) {
            return E_2HIGH;
        }
	v1.v.val /= v2.v.val;
	PushValStack(v1);
	return OK;
    }
    DestroyValue(v1); DestroyValue(v2);
    return E_BAD_TYPE;
}

/***************************************************************/
/*                                                             */
/*  Mod                                                        */
/*                                                             */
/*  Perform modulus function.                                  */
/*                                                             */
/***************************************************************/
static int Mod(void)
{
    Value v1, v2;
    int r;

    PopValStack(v2);
    if ( (r = FnPopValStack(&v1)) ) {
	DestroyValue(v2);
	return r;
    }

    if (v1.type == INT_TYPE && v2.type == INT_TYPE) {
	if (v2.v.val == 0) return E_DIV_ZERO;
	v1.v.val %= v2.v.val;
	PushValStack(v1);
	return OK;
    }
    DestroyValue(v1); DestroyValue(v2);
    return E_BAD_TYPE;
}


/***************************************************************/
/*                                                             */
/*  GreaterThan, LessThan, EqualTo, NotEqual, LessOrEqual,     */
/*  GreaterOrEqual                                             */
/*                                                             */
/*  All the comparison functions.                              */
/*                                                             */
/***************************************************************/
static int GreaterThan(void) {return Compare(GT);}
static int LessThan(void) {return Compare(LT);}
static int EqualTo(void) {return Compare(EQ);}
static int NotEqual(void) {return Compare(NE);}
static int LessOrEqual(void) {return Compare(LE);}
static int GreaterOrEqual(void) {return Compare(GE);}

/***************************************************************/
/*                                                             */
/*  Compare                                                    */
/*  Do the actual work of comparison.                          */
/*                                                             */
/***************************************************************/
static int Compare(int how)
{
    Value v1, v2, v3;
    int r;

    PopValStack(v2);
    if ( (r = FnPopValStack(&v1)) ) {
	DestroyValue(v2);
	return r;
    }

/* Special case for EQ and NE */

    v3.type = INT_TYPE;
    if (v1.type != v2.type) {
	DestroyValue(v1); DestroyValue(v2);
	if (how == EQ) {
	    v3.v.val = 0;
	    PushValStack(v3);
	    return OK;
	} else if (how == NE) {
	    v3.v.val = 1;
	    PushValStack(v3);
	    return OK;
	} else return E_BAD_TYPE;
    }

    if (v1.type == STR_TYPE) {
	switch(how) {
	case EQ: v3.v.val = (strcmp(v1.v.str, v2.v.str) == 0); break;
	case NE: v3.v.val = (strcmp(v1.v.str, v2.v.str) != 0); break;
	case LT: v3.v.val = (strcmp(v1.v.str, v2.v.str) < 0); break;
	case GT: v3.v.val = (strcmp(v1.v.str, v2.v.str) > 0); break;
	case LE: v3.v.val = (strcmp(v1.v.str, v2.v.str) <= 0); break;
	case GE: v3.v.val = (strcmp(v1.v.str, v2.v.str) >= 0); break;
	}
    } else {
	switch(how) {
	case EQ: v3.v.val = (v1.v.val == v2.v.val); break;
	case NE: v3.v.val = (v1.v.val != v2.v.val); break;
	case LT: v3.v.val = (v1.v.val < v2.v.val); break;
	case GT: v3.v.val = (v1.v.val > v2.v.val); break;
	case LE: v3.v.val = (v1.v.val <= v2.v.val); break;
	case GE: v3.v.val = (v1.v.val >= v2.v.val); break;
	}
    }
    DestroyValue(v1); DestroyValue(v2);
    PushValStack(v3);
    return OK;
}

/***************************************************************/
/*                                                             */
/*  LogOR                                                      */
/*                                                             */
/*  Do logical OR                                              */
/*                                                             */
/***************************************************************/
static int LogOR(void)
{
    Value v1, v2;
    int r;

    PopValStack(v2);
    if ( (r = FnPopValStack(&v1)) ) {
	DestroyValue(v2);
	return r;
    }

    if (v1.type != v2.type || v1.type == STR_TYPE || v2.type == STR_TYPE) {
        DestroyValue(v1); DestroyValue(v2);
        return E_BAD_TYPE;
    }

    if (v1.v.val == 0) {
        v1.v.val = v2.v.val;
    }
    PushValStack(v1);
    return OK;
}

/***************************************************************/
/*                                                             */
/*  LogAND                                                     */
/*                                                             */
/*  Do logical AND                                             */
/*                                                             */
/***************************************************************/
static int LogAND(void)
{
    Value v1, v2;
    int r;

    PopValStack(v2);
    if ( (r = FnPopValStack(&v1)) ) {
	DestroyValue(v2);
	return r;
    }

    if (v1.type != v2.type || v1.type == STR_TYPE || v2.type == STR_TYPE) {
        DestroyValue(v1); DestroyValue(v2);
        return E_BAD_TYPE;
    }
    if (v1.v.val != 0) {
        v1.v.val = v2.v.val;
    }
    PushValStack(v1);
    return OK;
}

/***************************************************************/
/*                                                             */
/*  UnMinus                                                    */
/*                                                             */
/*  Unary Minus                                                */
/*                                                             */
/***************************************************************/
static int UnMinus(void)
{
    Value *v = &ValStack[ValStackPtr-1];
    if (v->type != INT_TYPE) return E_BAD_TYPE;
    if (v->v.val == INT_MIN) return E_2HIGH;
    v->v.val = -v->v.val;
    return OK;
}

/***************************************************************/
/*                                                             */
/*  LogNot                                                     */
/*                                                             */
/*  Logical NOT                                                */
/*                                                             */
/***************************************************************/
static int LogNot(void)
{
    Value *v = &ValStack[ValStackPtr-1];
    if (v->type != INT_TYPE) return E_BAD_TYPE;
    if (v->v.val) v->v.val = 0; else v->v.val = 1;
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FindOperator                                               */
/*                                                             */
/*  Find an operator.                                          */
/*                                                             */
/***************************************************************/
Operator *FindOperator(char const *name, Operator where[], int num)
{
    int top=num-1, bot=0;
    int mid, r;
    while (top >= bot) {
	mid = (top + bot) / 2;
	r = strcmp(name, where[mid].name);
	if (!r) return &where[mid];
	else if (r > 0) bot = mid+1;
	else top = mid-1;
    }
    return NULL;
}

/* Compare two strings case-insensitively, where we KNOW
   that the second string is definitely lower-case */
static int strcmp_lcfirst(char const *s1, char const *s2)
{
    int r;
    while (*s1 && *s2) {
	r = tolower(*s1) - *s2;
	if (r) return r;
	s1++;
	s2++;
    }
    return tolower(*s1) - *s2;
}

/***************************************************************/
/*                                                             */
/*  FindFunc                                                   */
/*                                                             */
/*  Find a function.                                           */
/*                                                             */
/***************************************************************/
BuiltinFunc *FindFunc(char const *name, BuiltinFunc where[], int num)
{
    int top=num-1, bot=0;
    int mid, r;
    while (top >= bot) {
	mid = (top + bot) / 2;
	r = strcmp_lcfirst(name, where[mid].name);
	if (!r) return &where[mid];
	else if (r > 0) bot = mid+1;
	else top = mid-1;
    }
    return NULL;
}

/***************************************************************/
/*                                                             */
/*  PrintValue                                                 */
/*                                                             */
/*  Print a value to stdout for debugging purposes.            */
/*                                                             */
/***************************************************************/
void PrintValue (Value *v, FILE *fp)
{
    int y, m, d;
    unsigned char const *s;

    if (v->type == STR_TYPE) {
	s = (unsigned char const *) v->v.str;
	putc('"', fp);
	for (y=0; y<MAX_PRT_LEN && *s; y++) {
            switch(*s) {
            case '\a': fprintf(ErrFp, "\\a"); break;
            case '\b': fprintf(ErrFp, "\\b"); break;
            case '\f': fprintf(ErrFp, "\\f"); break;
            case '\n': fprintf(ErrFp, "\\n"); break;
            case '\r': fprintf(ErrFp, "\\r"); break;
            case '\t': fprintf(ErrFp, "\\t"); break;
            case '\v': fprintf(ErrFp, "\\v"); break;
            case '"':  fprintf(ErrFp, "\\\""); break;
            case '\\':  fprintf(ErrFp, "\\\\"); break;
            default:
                if (*s < 32) {
                    fprintf(ErrFp, "\\x%02x", (unsigned int) *s);
                } else {
                    putc(*s, ErrFp); break;
                }
            }
            s++;
        }
	putc('"',fp);
	if (*s) fprintf(fp, "...");
    }
    else if (v->type == INT_TYPE) fprintf(fp, "%d", v->v.val);
    else if (v->type == TIME_TYPE) fprintf(fp, "%02d%c%02d", v->v.val / 60,
					   TimeSep, v->v.val % 60);
    else if (v->type == DATE_TYPE) {
	FromJulian(v->v.val, &y, &m, &d);
	fprintf(fp, "%04d%c%02d%c%02d", y, DateSep, m+1, DateSep, d);
    }
    else if (v->type == DATETIME_TYPE) {
	FromJulian(v->v.val / MINUTES_PER_DAY, &y, &m, &d);
	fprintf(fp, "%04d%c%02d%c%02d%c%02d%c%02d", y, DateSep, m+1, DateSep, d, DateTimeSep,
		(v->v.val % MINUTES_PER_DAY) / 60, TimeSep, (v->v.val % MINUTES_PER_DAY) % 60);
    }
    else fprintf(fp, "ERR");
}

/***************************************************************/
/*                                                             */
/*  CopyValue                                                  */
/*                                                             */
/*  Copy a value.                                              */
/*                                                             */
/***************************************************************/
int CopyValue(Value *dest, const Value *src)
{
    dest->type = ERR_TYPE;
    if (src->type == STR_TYPE) {
	dest->v.str = StrDup(src->v.str);
	if (!dest->v.str) return E_NO_MEM;
    } else {
	dest->v.val = src->v.val;
    }
    dest->type = src->type;
    return OK;
}

int ParseLiteralTime(char const **s, int *tim)
{
    int h=0;
    int m=0;
    int ampm=0;
    if (!isdigit(**s)) return E_BAD_TIME;
    while(isdigit(**s)) {
	h *= 10;
	h += *(*s)++ - '0';
    }
    if (**s != ':' && **s != '.' && **s != TimeSep) return E_BAD_TIME;
    (*s)++;
    if (!isdigit(**s)) return E_BAD_TIME;
    while(isdigit(**s)) {
	m *= 10;
	m += *(*s)++ - '0';
    }
    /* Check for p[m] or a[m] */
    if (**s == 'A' || **s == 'a' || **s == 'P' || **s == 'p') {
	ampm = tolower(**s);
	(*s)++;
	if (**s == 'm' || **s == 'M') {
	    (*s)++;
	}
    }
    if (h>23 || m>59) return E_BAD_TIME;
    if (ampm) {
	if (h < 1 || h > 12) return E_BAD_TIME;
	if (ampm == 'a') {
	    if (h == 12) {
		h = 0;
	    }
	} else if (ampm == 'p') {
	    if (h < 12) {
		h += 12;
	    }
	}
    }
    *tim = h * 60 + m;
    return OK;
}

/***************************************************************/
/*                                                             */
/*  ParseLiteralDate                                           */
/*                                                             */
/*  Parse a literal date or datetime.  Return result in jul    */
/*  and tim; update s.                                         */
/*                                                             */
/***************************************************************/
int ParseLiteralDate(char const **s, int *jul, int *tim)
{
    int y, m, d;
    int r;

    y=0; m=0; d=0;

    *tim = NO_TIME;
    if (!isdigit(**s)) return E_BAD_DATE;
    while (isdigit(**s)) {
	y *= 10;
	y += *(*s)++ - '0';
    }
    if (**s != '/' && **s != '-' && **s != DateSep) return E_BAD_DATE;
    (*s)++;
    if (!isdigit(**s)) return E_BAD_DATE;
    while (isdigit(**s)) {
	m *= 10;
	m += *(*s)++ - '0';
    }
    m--;
    if (**s != '/' && **s != '-' && **s != DateSep) return E_BAD_DATE;
    (*s)++;
    if (!isdigit(**s)) return E_BAD_DATE;
    while (isdigit(**s)) {
	d *= 10;
	d += *(*s)++ - '0';
    }
    if (!DateOK(y, m, d)) return E_BAD_DATE;

    *jul = Julian(y, m, d);

    /* Do we have a time part as well? */
    if (**s == ' ' || **s == '@' || **s == 'T' || **s == 't') {
	(*s)++;
	r = ParseLiteralTime(s, tim);
	if (r != OK) return r;
    }
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FnPopValStack                                              */
/*                                                             */
/*  Pop a value from the value stack - implemented as a        */
/*  function for situations where we don't want an immediate   */
/*  return upon failure.                                       */
/*                                                             */
/***************************************************************/
int FnPopValStack(Value *val)
{
    if (ValStackPtr <= 0)
	return E_VA_STK_UNDER;
    else {
	*val = ValStack[--ValStackPtr];
	return OK;
    }
}
