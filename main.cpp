#include <iostream>
#include <string>
#include <vector>
#include <list>
#include <map>

#include <boost/variant.hpp>
#include <boost/algorithm/string.hpp>

namespace lisp {

typedef int64_t lisp_int_t;

class lisp_cell;
class lambda;
class symbol;
struct environment; // forward declaration; cell and environment reference each other

typedef std::map<std::string, lisp_cell *> sym_map;
typedef lisp_cell *(*proc_type)(lisp_cell *, environment *);

std::string printLispObject(lisp_cell *sexpr);
std::string printLispTree(lisp_cell *sexpr);

lisp_cell *eval(lisp_cell *sexpr, environment *env);

std::vector<std::string> undefined_symbols;

class lisp_cells {
    lisp_cell *_car;
    lisp_cell *_cdr;
public:
    lisp_cells(lisp_cell *car, lisp_cell *cdr): _car(car), _cdr(cdr) {}
    
    lisp_cell *car(void) { return _car; }
    lisp_cell *cdr(void) { return _cdr; }
};

class lisp_cell {
public:
    boost::variant<
        // following are atoms
        lisp_int_t,
        double,
        std::string,    // std:string is for symbol
        const char *,   // const char * is for "quoted string"
        proc_type,

        // lisp cells
        lisp_cells,
        lambda *
    > node;

    lisp_cell(lisp_cell *car, lisp_cell *cdr = nullptr) // : env(0)
    {
        lisp_cells cells(car, cdr);
        node = cells;
    }
    
    lisp_cell(lisp_int_t &n):   node(n) {}
    lisp_cell(double &d):       node(d) {}
    lisp_cell(const char *s):   node(s) {}
    lisp_cell(proc_type p):     node(p) {}
    lisp_cell(std::string &s):  node(s) {}
    lisp_cell(lambda *l):       node(l) {}
    
    template <typename T>
    bool getValue(T &value)
    {
        if (node.type() == typeid(T))
        {
            value = boost::get<T>(node);
            return true;
        }
        return false;
    }
    
    bool isConstant(void)
    {
        return (node.type() == typeid(lisp_int_t)   ||
                node.type() == typeid(double)       ||
                node.type() == typeid(const char *));
    }
       
    lisp_cell *car(void)
    {
        if (isLispCells())
            return boost::get<lisp_cells>(node).car();
        return nullptr;
    }
    
    lisp_cell *cdr(void)
    {
        if (isLispCells())
            return boost::get<lisp_cells>(node).cdr();
        return nullptr;
    }
   
    bool isAtom(void)                   { return !isLispCells(); }
    bool isLispCells(void)              { return node.type() == typeid(lisp_cells); }
    bool isLambda(lambda* &l)           { return getValue<lambda *>(l); }
    bool isSymbol(std::string &s)       { return getValue<std::string>(s); }
};

class lambda: public lisp_cells {
    environment *env_;

public:
    lambda(lisp_cell *params, lisp_cell *body, environment *a_env): lisp_cells(params, body), env_(a_env) {}
    
    lisp_cell *params(void)             { return car(); }
    lisp_cell *body(void)               { return cdr(); }
    
    environment *env(void)              { return env_; }
};

// Environment is a dictionary that associates symbols with lisp_cells (symbol table), and chain to an "outer" dictionary.
// Use std::map (sym_map) for symbol table
struct environment {
    environment(environment *outer = 0) : outer_(outer) {}

    environment(lisp_cell *params, lisp_cell *args, environment *outer): outer_(outer)
    {
        while (params != nullptr)
        {
            std::string param;
            if (params->isSymbol(param))
            {
                env_[param] = eval(args, outer_);
                break;
            }
            if (params->car()->isSymbol(param))
                env_[param] = eval(args->car(), outer_);
            params = params->cdr();
            args = args->cdr();
        }
    }

    bool FindSymbol(std::string &s, lisp_cell * &lisp_cell)
    {
        auto iter = env_.find(s);
        if (iter != env_.end())
        {
            lisp_cell = iter->second;
            return true;
        }
        if (outer_)
            return outer_->FindSymbol(s, lisp_cell);
        
        if (std::find(undefined_symbols.begin(), undefined_symbols.end(), s) == undefined_symbols.end())
        {
            // eval might be called multiple times, e.g. proc_arith_impl/proc_compare_impl.
            // Using undefined_symbols eliminates multiple error messages.
            std::cout << "Undefined symbol '" << s << "'" << std::endl;
            undefined_symbols.push_back(s);
        }
        return false;
    }

    bool UpdateSymbol(std::string &s, lisp_cell *lisp_cell, bool current_scope_only)
    {
        bool result = true;
        auto iter = env_.find(s);
        
        // symbol exists, just update its value
        if (iter != env_.end())
            iter->second = lisp_cell;
        // define, always update/add in current scope
        else if (current_scope_only)
            env_[s] = lisp_cell;
        // set! only add if symbol exists in some scope
        else if (outer_)
            return outer_->UpdateSymbol(s, lisp_cell, false);
        else
            result = false;
        return result;
    }

    // return a reference to the cell associated with the given symbol 'var' in current environment
    lisp_cell * &operator[] (const std::string& var)
    {
        return env_[var];
    }
    
private:
    sym_map env_;           // inner symbol->cell mapping
    environment *outer_;    // next adjacent outer env, or 0 if there are no further environments
};

lisp_cell *make_builtin_symbol(const char *s)
{
    auto str = new std::string(s);
    return new lisp_cell(*str);
}

lisp_cell *false_sexpr = make_builtin_symbol("#f");
lisp_cell *true_sexpr  = make_builtin_symbol("#t");
lisp_cell *nil_sexpr   = make_builtin_symbol("#nil");
lisp_cell *bad_sexpr   = make_builtin_symbol("#error");

// Primitive Operations
bool hasTwoOperands(lisp_cell *sexpr)
{
    if (sexpr == nullptr || sexpr->car() == nullptr || sexpr->cdr() == nullptr)
        return false;
    return true;
}

typedef lisp_int_t (*binop_func)(lisp_int_t, lisp_int_t);
typedef bool       (*cmpop_func)(lisp_int_t, lisp_int_t);

// Arithmetic Primitives
bool proc_arith_impl(lisp_cell *sexpr, lisp_int_t &n, environment *env, binop_func Op)
{
    if (sexpr == nullptr)
        return false;
    
    if (sexpr->isAtom())
        return sexpr->getValue<lisp_int_t>(n);
    
    lisp_int_t n1;
    if (proc_arith_impl(eval(sexpr->car(), env), n1, env, Op) == false)
        return false;
    
    if (sexpr->cdr() == nullptr)
    {
        n = n1;
        return true;
    }
    lisp_int_t n2;
    
    // binop primitives (+-*/) can have a list of operands, e.g. (+ 1 2 3 4). Internally
    // it's still just a lisp cons tree.
    //
    // the inner nodes maybe an op, so it needs to be eval'ed, or
    // it could be atoms, which can be resolved if we recursive call the function
    // without evaluting it,
    lisp_cell *cdrval = eval(sexpr->cdr(), env);
    
    if (cdrval != nullptr && cdrval->getValue<lisp_int_t>(n2))
    {
        n = Op(n1, n2);
        return true;
    }
    
    if (proc_arith_impl(sexpr->cdr(), n2, env, Op) == false)
        return false;
    
    n = Op(n1, n2);
    return true;
    
}

lisp_cell *proc_arith_main(lisp_cell *sexpr, environment *env, binop_func Op)
{
    lisp_int_t n;
    if (proc_arith_impl(sexpr, n, env, Op))
        return new lisp_cell(n);
    return false_sexpr;
}

lisp_cell *proc_add(lisp_cell *sexpr, environment *env)
{
    return proc_arith_main(sexpr, env, [](lisp_int_t n1, lisp_int_t n2) { return n1+n2;});
}

lisp_cell *proc_sub(lisp_cell *sexpr, environment *env)
{
    return proc_arith_main(sexpr, env, [](lisp_int_t n1, lisp_int_t n2) { return n1-n2;});
}

lisp_cell *proc_mul(lisp_cell *sexpr, environment *env)
{
    return proc_arith_main(sexpr, env, [](lisp_int_t n1, lisp_int_t n2) { return n1*n2;});
}

lisp_cell *proc_div(lisp_cell *sexpr, environment *env)
{
    return proc_arith_main(sexpr, env, [](lisp_int_t n1, lisp_int_t n2) { return n1/n2;});
}

// Relational Primitives
bool proc_compare_impl(lisp_cell *sexpr, lisp_int_t &n, environment *env, cmpop_func Op)
{
    if (sexpr == nullptr)
        return false;
    
    if (sexpr->isAtom())
        return sexpr->getValue<lisp_int_t>(n);
    
    if (proc_compare_impl(eval(sexpr->car(), env), n, env, Op) == false)
        return false;
    
    if (sexpr->cdr() == nullptr)
        return true;
    
    lisp_int_t n2;
    // see comments in proc_arith_impl on how to handle multi-operand primitives
    lisp_cell *cdrval = eval(sexpr->cdr(), env);
     
    if (cdrval != nullptr && cdrval->getValue<lisp_int_t>(n2))
        return Op(n, n2);
     
    if (proc_compare_impl(sexpr->cdr(), n2, env, Op) == false)
        return false;
    return Op(n, n2);
}

lisp_cell *proc_compare_main(lisp_cell *sexpr, environment *env, cmpop_func Op)
{
    lisp_int_t n;
    if (proc_compare_impl(sexpr, n, env, Op))
        return true_sexpr;
    return false_sexpr;
}

lisp_cell *proc_cmpgt(lisp_cell *sexpr, environment *env)
{
    return proc_compare_main(sexpr, env, [](lisp_int_t n1, lisp_int_t n2) { return n1>n2;});
}

lisp_cell *proc_cmpge(lisp_cell *sexpr, environment *env)
{
    return proc_compare_main(sexpr, env, [](lisp_int_t n1, lisp_int_t n2) { return n1>=n2;});
}

lisp_cell *proc_cmplt(lisp_cell *sexpr, environment *env)
{
    return proc_compare_main(sexpr, env, [](lisp_int_t n1, lisp_int_t n2) { return n1<n2;});
}

lisp_cell *proc_cmple(lisp_cell *sexpr, environment *env)
{
    return proc_compare_main(sexpr, env, [](lisp_int_t n1, lisp_int_t n2) { return n1<=n2;});
}

lisp_cell *proc_cmpeq(lisp_cell *sexpr, environment *env)
{
    return proc_compare_main(sexpr, env, [](lisp_int_t n1, lisp_int_t n2) { return n1==n2;});
}

lisp_cell *proc_cmpne(lisp_cell *sexpr, environment *env)
{
    return proc_compare_main(sexpr, env, [](lisp_int_t n1, lisp_int_t n2) { return n1!=n2;});
}

// List Processing
lisp_cell *eval_car(lisp_cell *sexpr, environment *env)
{
    if (sexpr == nullptr)
        return nullptr;
    sexpr = eval(sexpr, env);
    if (sexpr == nullptr || sexpr->isAtom())
        return bad_sexpr;
    return sexpr->car();
}

lisp_cell *eval_cdr(lisp_cell *sexpr, environment *env)
{
    if (sexpr == nullptr)
        return nullptr;
    sexpr = eval(sexpr, env);
    if (sexpr == nullptr || sexpr->isAtom())
        return bad_sexpr;
    return sexpr->cdr();
}

lisp_cell *eval_cons(lisp_cell *sexpr, environment *env)
{
    lisp_cell *car = eval(sexpr->car(), env);
    lisp_cell *cdr = eval(sexpr->cdr(), env);
    return new lisp_cell(car, cdr);
}

lisp_cell *proc_append(lisp_cell *sexpr, lisp_cell *tail, environment *env)
{
    return nullptr;
}

lisp_cell *eval_list(lisp_cell *sexpr, environment *env)
{
    if (sexpr == nullptr)
        return nullptr;
    
    if (sexpr->isAtom())
        return eval(sexpr, env);
    
    lisp_cell *car = eval_list(sexpr->car(), env);
    lisp_cell *cdr = sexpr->cdr();
        
    if (cdr->isLispCells())
        cdr = eval_list(cdr, env);
    else
        cdr = new lisp_cell(eval(cdr, env), nullptr);
    
    return new lisp_cell(car, cdr);
}

// Eval Primitives
lisp_cell *eval_if(lisp_cell *sexpr, environment *env)
{
    if (!hasTwoOperands(sexpr))
        return bad_sexpr;
    
    lisp_cell *car = sexpr->car();
    lisp_cell *cdr = sexpr->cdr();
    
    lisp_cell *val = eval(car, env);
    if (val != false_sexpr)
        return eval(cdr->car(), env);
    return eval(cdr->cdr(), env);
}

// Handle both
//  define: current_scope_only
//  set!:   search up and scope and do not create variable if not found
lisp_cell *eval_set(lisp_cell *sexpr, environment *env, bool is_define)
{
    if (!hasTwoOperands(sexpr))
        return bad_sexpr;
    
    std::string s;
    if (sexpr->car()->getValue<std::string>(s) == false)
        return bad_sexpr;
    
    lisp_cell *val = eval(sexpr->cdr(), env);
    if (env->UpdateSymbol(s, val, is_define))
        return val;
    
    // only when is_define == false, setq
    std::cout << "Variable '" << s << "' does not exist." << std::endl;
    return nil_sexpr;
}

lisp_cell *eval_setq(lisp_cell *sexpr, environment *env)
{
    return eval_set(sexpr, env, false);
}

lisp_cell *eval_define(lisp_cell *sexpr, environment *env)
{
    return eval_set(sexpr, env, true);
}

lisp_cell *eval_begin(lisp_cell *sexpr, environment *env)
{
    if (sexpr == nullptr)
        return nullptr;
    if (sexpr->isAtom())
        return eval(sexpr, env);
    lisp_cell *val = eval(sexpr->car(), env);
    if (sexpr->cdr())
        val = eval(sexpr->cdr(), env);
    return val;
}

// Primitive functions
void add_globals(environment &env)
{
    env["nil"]      = nil_sexpr;
    env["#f"]       = false_sexpr;
    env["#t"]       = true_sexpr;
//    env["append"]   = new lisp_cell(&proc_append);

//    env["length"]   = new lisp_cell(&proc_length);
//    env["list"]     = new lisp_cell(&proc_list);
//    env["null?"]    = new lisp_cell(&proc_nullp);
    env["+"]        = new lisp_cell(&proc_add);
    env["-"]        = new lisp_cell(&proc_sub);
    env["*"]        = new lisp_cell(&proc_mul);
    env["/"]        = new lisp_cell(&proc_div);
    env[">"]        = new lisp_cell(&proc_cmpgt);
    env["<"]        = new lisp_cell(&proc_cmplt);
    env["<="]       = new lisp_cell(&proc_cmple);
    env[">="]       = new lisp_cell(&proc_cmpge);
    env["eq"]       = new lisp_cell(&proc_cmpeq);
    env["ne"]       = new lisp_cell(&proc_cmpne);
    
    env["begin"]    = new lisp_cell(&eval_begin);
    env["car"]      = new lisp_cell(&eval_car);
    env["cdr"]      = new lisp_cell(&eval_cdr);
    env["cons"]     = new lisp_cell(&eval_cons);
    env["define"]   = new lisp_cell(&eval_define);
    env["if"]       = new lisp_cell(&eval_if);
    env["list"]     = new lisp_cell(&eval_list);
    env["setq"]     = new lisp_cell(&eval_setq);
}

lisp_cell *eval_proc(lisp_cell *proc, lisp_cell *func_body, environment *env)
{
    proc_type native_func;
    
    if (proc->getValue<proc_type>(native_func))
        return native_func(func_body, env);
    
    return nullptr;
}

lisp_cell *eval_lambda(lambda *l, lisp_cell *args, environment *env)
{
    // std::cout << "params: " << printLispObject(l->params()) << " args: " << printLispObject(args) << std::endl;
    environment *new_env = new environment(l->params(), args, l->env());
    // std::cout << "body: " << printLispObject(l->body()) << std::endl;
    lisp_cell *val = eval(l->body(), new_env);
    delete(new_env);
    
    return val;
    
}
    
lisp_cell *makeLambda(lisp_cell *sexpr, environment *env)
{
    // ensure we have an parameter list, even if empty, and a body
    // (lambda (args) body)
    if (sexpr->cdr() == nullptr ||
        !sexpr->cdr()->isLispCells() ||
        (sexpr->cdr()->car() != nullptr &&          // params
         !sexpr->cdr()->car()->isLispCells()) ||
        sexpr->cdr()->cdr() == nullptr)             // body
        return nullptr;
                               // params            // body
    lambda *a_lambda = new lambda{sexpr->cdr()->car(), sexpr->cdr()->cdr(), env};
    
    return new lisp_cell(a_lambda);
}

// EVAL, evaluate an S-expression
lisp_cell *eval(lisp_cell *sexpr, environment *env)
{
    if (sexpr == nullptr || sexpr == nil_sexpr || sexpr == bad_sexpr || sexpr->isConstant())
        return sexpr;

    lisp_cell *val;
    std::string s;
    
    // Symbol
    if (sexpr->isAtom())
    {
        if (sexpr->isSymbol(s) && env->FindSymbol(s, val))
            return val;
        return nil_sexpr;
    }
    
    lisp_cell *car = sexpr->car();
    if (car == nullptr || car == nil_sexpr || car == bad_sexpr || car->isConstant())
        return bad_sexpr;
    
    if (car->isSymbol(s))
    {
        if (s == "quote")
            return sexpr->cdr();
        if (s == "lambda")
            return makeLambda(sexpr, env);
        
        if (env->FindSymbol(s, val) == false)
            return bad_sexpr;

        lambda *l;
        if (val->isLambda(l))
            return eval_lambda(l, sexpr->cdr(), env);
        return eval_proc(val, sexpr->cdr(), env);
    }
    /*
    else if (car->car() != nullptr && car->car()->isLispCells() && car->car()->car()->isSymbol(s) && s == "lambda")
    {
        lambda *lambda = makeLambda(car->car(), env);
        return eval_lambda(lambda, sexpr->cdr(), env);
    } */
    
    return nullptr;
}

// REPL and support functions

typedef std::vector<std::string> Tokens;

#define EOINPUT(c)  ((c) == 0 || (c) == '\n')

const char *ops = {"()[]{}:*/"};

void tokenize(Tokens &tokens, std::string &s)
{
    const char *next = s.c_str();
    
    while (isspace(*next) || !isprint(*next))
        next++;
    while (!EOINPUT(*next))
    {
        std::string str;
        
        str = "";

        if (strchr(ops, *next) != nullptr)
            str = *next++;
        // symbol
        else if (isalpha(*next) || *next == '_')
            while (isalnum(*next) || *next == '_')
                str += *next++;
        // #symbol
        else if (*next == '#' && isalpha(next[1]))
        {
            str += *next++;
            while (isalnum(*next) || *next == '_')
                str += *next++;
        }
        // integer
        else if (isdigit(*next) || ((*next == '+' || *next == '-') && isdigit(next[1])))
        {
            if (*next == '+' || *next == '-')
                str += *next++;
            int base = 10;
            if (*next == '0' && islower(next[1]) == 'x' && isxdigit(next[2]))
            {
                str += "0x";
                next += 2;
                base = 16;
            }
            while (isdigit(*next) || (base == 16 && isxdigit(*next)))
                str += *next++;
        }
        // + or -
        else if (*next == '+' || *next == '-')
            str += *next++;
        // < > >= <=
        else if (*next == '<' || *next == '>')
        {
            str += *next++;
            if (*next == '=')
                str += *next++;
        }
        // "quoted string"
        else if (*next == '"')
        {
            str += *next++;
            while (!EOINPUT(*next) && *next != '"')
            {
                str += *next;
                if (*next == '\'' && !EOINPUT(next[1]))
                    str += *++next;
                next++;
            }
            if (*next == '"')
                str += *next++;
        }
        if (str == "")
            std::cerr << "unknown character '" << *next++ << "' ignored." << std::endl;
        
        // anything else
        else
            tokens.push_back(str);
        while (!EOINPUT(*next) && (isspace(*next) || !isprint(*next)))
            next++;
    }
}

lisp_cell *makeLispObject(Tokens::iterator &token_stream, Tokens::iterator &end);
lisp_cell *makeLispTree(Tokens::iterator &token_stream, Tokens::iterator &end);

// A Lisp Object pseudo BNF:
// lisp_object = symbol | constant | '(' lisp_tree | ')' : nil
// lisp_tree   = lisp_object lisp_tree : cons(_1, _2)
//
lisp_cell *makeLispObject(Tokens::iterator &token_stream, Tokens::iterator &end)
{
    if (token_stream == end)
        return nullptr;
    std::string token = *token_stream;
    
    if (isdigit(token[0]))
    {
        int64_t n = strtoll(&token[0], 0, 0);
        return new lisp_cell(n);
    }
    if (token[0] == '"')
    {
        const char *s = strdup((char *)token.c_str());
        return new lisp_cell(s);
    }
    if (token[0] != '(')
    {
        boost::to_lower(token);
        return new lisp_cell(token);
    }
    /*
    if ((*(token_stream+1))[0] == ')')
    {
        token_stream++;
        return new lisp_cell(nullptr, nullptr); // nil_sexpr;
    } */
    return makeLispTree(token_stream, end);
}

lisp_cell *makeLispTree(Tokens::iterator &token_stream, Tokens::iterator &end)
{
    std::string token = *++token_stream;
    if (token[0] == ')')
        return nullptr;
    lisp_cell *car = makeLispObject(token_stream, end);
    lisp_cell *cdr = makeLispTree(token_stream, end);
    
    if (cdr == nullptr)
        return car;
    
    return new lisp_cell(car, cdr);
}

// convert a Lisp tree to a string
std::string printLispTree(lisp_cell *sexpr)
{
    std::string s = printLispObject(sexpr->car());
    
    if (sexpr->cdr() == nullptr || sexpr->cdr() == nil_sexpr)
        s += ")";
    else if (sexpr->cdr()->isLispCells())
        s += " " + printLispTree(sexpr->cdr());
    else
        s += " . " + printLispObject(sexpr->cdr()) + ")";
    return s;
}

// convert a Lisp object to a string
std::string printLispObject(lisp_cell *sexpr)
{
    if (sexpr == nullptr)
        return "null";
    
    lambda *l;
    if (sexpr->isLambda(l))
        return "<Lambda>";
    
    if (sexpr->isLispCells())
        return "(" + printLispTree(sexpr);

    lisp_int_t n;
    double d;
    const char *s;
    std::string str;
    
    char buf[30];
    if (sexpr->getValue<lisp_int_t>(n))
    {
        sprintf(buf, "%lld", n);
        return buf;
    }
    if (sexpr->getValue<double>(d))
    {
        sprintf(buf, "%f", d);
        return buf;
    }
    if (sexpr->getValue<const char *>(s))
        return std::string(s);
    if (sexpr->getValue<std::string>(str))
        return str;
    return "bad symbol";
}

// the default read-eval-print-loop
void repl(const std::string &prompt, environment *env)
{
    for (;;)
    {
        std::cout << prompt;
        
        // get input and convert to tokens (vector of std:string)
        std::string line;   std::getline(std::cin, line);
        Tokens tokens;      tokenize(tokens, line);
        
        // check to make sure the parentheses are balanced
        int nparens = 0;
        for (auto &token: tokens)
            if (token[0] == '(')
                nparens++;
            else if (token[0] == ')')
                nparens--;
        if (nparens != 0)
        {
            std::cout << "Unbalanced parentheses." << std::endl;
            continue;
        }
        
        // Convert tokens into internal lisp trees
        Tokens::iterator it_next = tokens.begin();
        Tokens::iterator it_end  = tokens.end();
        
        lisp_cell *sexpr = makeLispObject(it_next, it_end);
        std::cout << '"' << printLispObject(sexpr) << '"' << std::endl;
        
        undefined_symbols.clear();
        std::cout << printLispObject(eval(sexpr, env)) << std::endl;
        if (it_next != it_end-1)
            std::cout << "extraneous input: " << *it_next << "..." << std::endl;
    }
}

} // Lisp namespace

int main ()
{
    lisp::environment global_env;     lisp::add_globals(global_env);
    lisp::repl("L> ", &global_env);
}

