
#include <stdio.h>
#include <assert.h>
#include <vector>
#include <stdint.h>

#include "ir.h"

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;

typedef int64_t  s64;
typedef int32_t  s32;
typedef int16_t  s16;
typedef int8_t   s8;

char *get_file_contents(char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return nullptr;

    fseek(file, 0, SEEK_END);
    auto len = ftell(file);
    assert(len != -1);
    fseek(file, 0, SEEK_SET);

    size_t amount = static_cast<size_t>(len);
    char *buffer = (char *)malloc(amount+1);

    do {
        size_t read = fread(buffer, 1, amount, file);
        amount -= read;
    } while (amount);

    buffer[len] = 0;
    return buffer;
}

struct Token {
    enum Type {
        END = 256,
        IDENTIFIER,
        INTEGER,
        FLOAT,
        STRING,

        KEYWORD_START = 500,
        KEYWORD_FUNC = KEYWORD_START,
        KEYWORD_VAR,
        KEYWORD_F64,
        KEYWORD_F32,
        KEYWORD_I64,
        KEYWORD_I32,
        KEYWORD_I16,
        KEYWORD_I8,
        KEYWORD_RETURN,
        KEYWORD_WHILE,

        KEYWORD_END,

        ARROW, // ->
    };

    u32 type;

    union {
        char  *string_value;
        u64    integer_value;
        double float_value;
    };

    bool is_float32 = false;
};

Token token(u32 type) {
    Token t;
    t.type = type;
    return t;
}

char *keywords[] = {
    "func",
    "var",
    "f64",
    "f32",
    "i64",
    "i32",
    "i16",
    "i8",
    "return",
    "while",
};

bool starts_identifier(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c == '_');
}

bool continues_identifer(char c) {
    return starts_identifier(c) || (c >= '0' && c <= '9');
}

bool is_whitespace(char c) {
    return c == ' ' || c == '\r' || c == '\t' || c == '\v' || c == '\n';
}

struct Lexer {
    char *buffer;

    Token next_token() {
        while (true) {
            char *start = buffer;
            // skip comments.
            if (*buffer == '/' && *(buffer+1) == '/') {
                while (*buffer != '\n' && *buffer != 0) buffer += 1;
            }

            while (*buffer && is_whitespace(*buffer)) buffer += 1;

            if (*buffer == 0) return token(Token::END);

            if (start == buffer) break;
        }

        if (starts_identifier(*buffer)) {
            char *end = buffer;

            while (*end) {
                if (continues_identifer(*end)) end += 1;
                else break;
            }

            auto length = end - buffer;
            u32 type = Token::IDENTIFIER;
            for (u32 i = 0; i < (Token::KEYWORD_END - Token::KEYWORD_START); ++i) {
                if (strncmp(buffer, keywords[i], strlen(keywords[i])) == 0) {
                    type = Token::KEYWORD_START + i;
                    break;
                }
            }

            Token t = token(type);

            if (type == Token::IDENTIFIER) {
                char *value = (char *)malloc(length + 1); // @Leak
                memcpy(value, buffer, length);
                value[length] = 0;
                t.string_value = value;
            }
            
            buffer = end;
            return t;
        } else if (*buffer == '"') {
            buffer += 1;
            char *end = buffer;
            while (*end && *end != '"') end += 1;

            Token t = token(Token::STRING);

            auto length = end - buffer;
            char *value = (char *)malloc(length + 1); // @Leak
            t.string_value = value;

            for (char *start = buffer; start < end; start += 1) {
                if (*start == '\\') {
                    start += 1;

                    switch (*start) {
                        case 'n':
                            *value = '\n';
                            break;

                        default:
                            *value = *start;
                            break;
                    }
                } else {
                    *value = *start;
                }

                value += 1;
            }
            *value = 0;

            buffer = end+1;
            return t;
        } else if (*buffer >= '0' && *buffer <= '9') {
            char *start = buffer;

            bool _float = false;
            while (*start) {
                if (*start >= '0' && *start <= '9') {
                    start += 1;
                    continue;
                }

                if (*start == '.') _float = true;

                break;
            }

            if (_float) {
                Token tok = token(Token::FLOAT);
                tok.float_value = strtod(buffer, &buffer);

                if (*buffer == 'f') {
                    tok.is_float32 = true;
                    buffer += 1;
                }
                return tok;
            } else {
                Token tok = token(Token::INTEGER);
                tok.integer_value = strtoul(buffer, &buffer, 10);
                return tok;
            }
        } else if (*buffer == '-' && *(buffer+1) == '>') {
            buffer += 2;
            return token(Token::ARROW);
        }

        Token t = token(*buffer);
        buffer += 1;
        return t;
    }

    void tokenize_text(std::vector<Token> *output) {
        Token tok;
        do {
            tok = next_token();
            output->push_back(tok);
        } while (tok.type != Token::END);
    }
};

struct Instruction_Alloca;

namespace AST {
    enum {
        INVALID,
        FUNCTION_CALL,
        LITERAL,
        VARIABLE,
        IDENTIFIER,
        BINARY_EXPRESSION,
        UNARY_EXPRESSION,
        RETURN,
        WHILE,
    };

    struct Expression {
        u32   type;
        Type *expr_type;

        bool is_lvalue_use = false;
    };

    struct Return : Expression {
        Return() { type = RETURN; }

        Expression *return_value = nullptr;
    };

    struct Binary_Expression : Expression {
        Binary_Expression() { type = BINARY_EXPRESSION; }

        u32 operator_type;

        Expression *lhs;
        Expression *rhs;
    };

    struct Unary_Expression : Expression {
        Unary_Expression() { type = UNARY_EXPRESSION; }

        u32 operator_type;

        Expression *rhs;
    };

    struct Literal : Expression {
        Literal() { type = LITERAL; }

        enum {
            STRING,
            INTEGER,
            FLOAT,
        };

        u32 literal_type;

        union {
            char * string_value;
            u64    integer_value;
            double float_value;
        };
    };

    struct Variable : Expression {
        Variable() { type = VARIABLE; }
        char *name;

        Expression *initializer = nullptr;
        Value *storage;
    };

    struct Identifier : Expression {
        Identifier() { type = IDENTIFIER; }

        char *name;

        Variable *variable;
    };

    struct Function_Call : Expression {
        Function_Call() { type = FUNCTION_CALL; }

        char *call_target_name;
        std::vector<Expression *> arguments;
    };

    struct Scope {
        std::vector<Expression *> statements;
    };

    struct While : Expression {
        While() { type = WHILE; }

        Expression *condition;
        Scope *body;
    };

    struct Function {
        char *name;

        std::vector<Variable *> arguments;
        Type *return_type;

        Scope *body;
    };


};

struct Parser {
    std::vector<Token> tokens;
    s32 token_cursor = 0;

    void next_token() {
        if (token_cursor < (tokens.size()-1)) token_cursor += 1;
    }

    Token peek_token(s32 offset = 0) {
        s32 index = token_cursor + offset;
        if (index >= (tokens.size()-1)) index = tokens.size()-1;
        return tokens[index];
    }

    bool expect_and_eat(u32 type) {
        bool result = expect(type);
        next_token();
        return result;
    }

    bool expect(u32 type) {
        Token t = peek_token();
        if (t.type != type) {
            printf("Expected token %d but got %d\n", type, t.type);
            return false;
        }

        return true;
    }

    AST::Function_Call *parse_function_call() {
        if (!expect(Token::IDENTIFIER)) return nullptr;

        Token name = peek_token();
        next_token();

        AST::Function_Call *call = new AST::Function_Call();
        call->call_target_name = name.string_value;

        if (!expect_and_eat('(')) return nullptr;

        while (peek_token().type != ')') {
            call->arguments.push_back(parse_expression());
            if (peek_token().type == ',') next_token();
        }

        if (!expect_and_eat(')')) return nullptr;

        return call;
    }

    AST::Expression *parse_primary_expression() {
        if (peek_token().type == '(') {
            next_token();
            auto result = parse_expression();
            if (!expect_and_eat(')')) return nullptr;
            return result;
        }

        if (peek_token().type == Token::IDENTIFIER) {
            if (peek_token(1).type == '(') return parse_function_call();

            AST::Identifier *ident = new AST::Identifier();
            ident->name = peek_token().string_value;
            next_token();
            return ident;
        }

        if (peek_token().type == Token::STRING) {
            AST::Literal *lit = new AST::Literal();

            Token t = peek_token();
            lit->literal_type = AST::Literal::STRING;
            lit->string_value = t.string_value;

            next_token();
            return lit;
        }

        if (peek_token().type == Token::INTEGER) {
            AST::Literal *lit = new AST::Literal();

            Token t = peek_token();
            lit->literal_type = AST::Literal::INTEGER;
            lit->integer_value = t.integer_value;

            next_token();
            return lit;
        }

        if (peek_token().type == Token::FLOAT) {
            AST::Literal *lit = new AST::Literal();

            Token t = peek_token();
            lit->literal_type = AST::Literal::FLOAT;
            lit->float_value = t.float_value;
            lit->expr_type = make_float_type(t.is_float32 ? 4 : 8);

            next_token();
            return lit;
        }
    }

    AST::Expression *parse_unary_expression() {
        if (peek_token().type == '*' || peek_token().type == '<') {
            AST::Unary_Expression *unary = new AST::Unary_Expression();
            unary->operator_type = peek_token().type;
            next_token();

            unary->rhs = parse_unary_expression();
            unary->rhs->is_lvalue_use = (unary->operator_type == '*');
            return unary;
        }

        return parse_primary_expression();
    }

    AST::Expression *parse_multiplicative_expression() {
        AST::Expression *expr = parse_unary_expression();

        while ((peek_token().type == '*') ||  (peek_token().type == '/') || (peek_token().type == '\\')) {
            AST::Binary_Expression *bin = new AST::Binary_Expression();
            bin->operator_type = peek_token().type;

            next_token();

            bin->lhs = expr;
            bin->rhs = parse_unary_expression();

            expr = bin;
        }

        return expr;
    }


    AST::Expression *parse_binary_expression() {
        AST::Expression *expr = parse_multiplicative_expression();

        while ((peek_token().type == '+') || (peek_token().type == '-')) {
            AST::Binary_Expression *bin = new AST::Binary_Expression();
            bin->operator_type = peek_token().type;

            next_token();

            bin->lhs = expr;
            bin->rhs = parse_multiplicative_expression();

            expr = bin;
        }

        return expr;
    }

    AST::Expression *parse_expression() {
        return parse_binary_expression();
    }

    Type *parse_type_spec() {
        if (peek_token().type == '*') {
            next_token();
            return make_pointer_type(parse_type_spec());
        }

        switch (peek_token().type) {
            case Token::KEYWORD_F64: next_token(); return make_float_type(8);
            case Token::KEYWORD_F32: next_token(); return make_float_type(4);
            case Token::KEYWORD_I64: next_token(); return make_integer_type(8);
            case Token::KEYWORD_I32: next_token(); return make_integer_type(4);
            case Token::KEYWORD_I16: next_token(); return make_integer_type(2);
            case Token::KEYWORD_I8 : next_token(); return make_integer_type(1);
            
            default: break;
        }

        printf("Invalid type specfifer: %d\n", peek_token().type);
        return nullptr;
    }

    AST::Variable *parse_var(bool param = false) {
        if (!param) {
            if (!expect_and_eat(Token::KEYWORD_VAR)) return nullptr;
        }

        if (!expect(Token::IDENTIFIER)) return nullptr;

        AST::Variable *var = new AST::Variable();
        var->name = peek_token().string_value;

        next_token();

        if (!expect_and_eat(':')) {
            printf("Missing type specification for variable '%s'.\n", var->name);
            return nullptr;
        }

        var->expr_type = parse_type_spec();
        if (!var->expr_type) {
            printf("Expected type specification following ':' in var declaration.\n");
            return nullptr;
        }

        if (!param) {
            if (peek_token().type == '=') {
                next_token();

                var->initializer = parse_expression();
            }

            if (!expect_and_eat(';')) return nullptr;
        }
        return var;
    }

    AST::Expression *parse_statement() {
        if (peek_token().type == Token::KEYWORD_VAR) {
            return parse_var();
        } else if (peek_token().type == Token::KEYWORD_RETURN) {
            AST::Return *ret = new AST::Return();
            next_token();

            if (peek_token().type  != ';') {
                ret->return_value = parse_expression();
            }

            if (!expect_and_eat(';')) return nullptr;

            return ret;
        } else if (peek_token().type == Token::KEYWORD_WHILE) {
            next_token();

            AST::While *wh = new AST::While();
            wh->condition = parse_expression();
            wh->body      = parse_scope();
            return wh;
        }


        AST::Expression *expr = parse_expression();

        if (peek_token().type == '=') {
            AST::Binary_Expression *bin = new AST::Binary_Expression();
            bin->operator_type = peek_token().type;

            next_token();

            bin->lhs = expr;
            bin->rhs = parse_expression();

            bin->lhs->is_lvalue_use = true;

            expr = bin;
        }

        if (!expect_and_eat(';')) return nullptr;

        return expr;
    }

    AST::Scope *parse_scope() {
        if (!expect_and_eat('{')) return nullptr;

        AST::Scope *scope = new AST::Scope();

        while (peek_token().type != '}') {
            AST::Expression *expr = parse_statement();
            if (expr) scope->statements.push_back(expr);
        }

        if (!expect_and_eat('}')) return nullptr;

        return scope;
    }


    AST::Function *parse_function() {
        if (!expect_and_eat(Token::KEYWORD_FUNC)) return nullptr;
        if (!expect(Token::IDENTIFIER)) return nullptr;

        AST::Function *function = new AST::Function();
        function->name = peek_token().string_value;
        next_token();

        if (!expect_and_eat('(')) return nullptr;

        while (peek_token().type != ')') {

            AST::Variable *var = parse_var(true);
            function->arguments.push_back(var);

            if (peek_token().type == ',') {
                next_token();
                continue;
            }

            break;
        }

        if (!expect_and_eat(')')) return nullptr;

        if (peek_token().type == Token::ARROW) {
            next_token();

            function->return_type = parse_type_spec();
        } else {
            function->return_type = make_void_type();
        }

        if (peek_token().type == '{') {
            function->body = parse_scope();
        } else {
            if (!expect_and_eat(';')) return nullptr;
        }

        return function;
    }
};

struct IR_Man : IR_Manager {
    Function *debugbreak = nullptr;
};

void emit_scope(AST::Scope *scope, AST::Function *function, Compilation_Unit *unit, Function *irfunc, IR_Man *irm);

Value *emit_expression(AST::Expression *expr, AST::Function *function, Compilation_Unit *unit, Function *irfunc, IR_Man *irm) {
    switch (expr->type) {
        case AST::LITERAL: {
            auto lit = static_cast<AST::Literal *>(expr);

            switch (lit->literal_type) {
                case AST::Literal::STRING : return make_string_constant(lit->string_value);
                case AST::Literal::INTEGER: return make_integer_constant(lit->integer_value);
                case AST::Literal::FLOAT  : return make_float_constant(lit->float_value, lit->expr_type);
            }
            assert(false);
            return nullptr;
        }

        case AST::BINARY_EXPRESSION: {
            auto bin = static_cast<AST::Binary_Expression *>(expr);

            auto left  = emit_expression(bin->lhs, function, unit, irfunc, irm);
            auto right = emit_expression(bin->rhs, function, unit, irfunc, irm);

            if (bin->operator_type == '=') {
                return irm->insert_store(right, left);
            } else {
                switch(bin->operator_type) {
                    case '+':  return irm->insert_add(left, right);
                    case '-':  return irm->insert_sub(left, right);
                    case '*':  return irm->insert_mul(left, right);
                    case '/':  return irm->insert_div(left, right, true);
                    case '\\': return irm->insert_div(left, right, false);
                    default: assert(false);
                }
            }

            return nullptr;
        }

        case AST::UNARY_EXPRESSION: {
            auto unary = static_cast<AST::Unary_Expression *>(expr);

            auto right = emit_expression(unary->rhs, function, unit, irfunc, irm);

            switch (unary->operator_type) {
                case '*': return right;
                case '<': {
                    if (unary->is_lvalue_use) return right;
                    return irm->insert_load(right);
                }
                default: assert(false); return nullptr;
            }
        }

        case AST::FUNCTION_CALL: {
            auto call = static_cast<AST::Function_Call *>(expr);

            Array<Value *> args;

            for (auto a : call->arguments) {
                args.add(emit_expression(a, function, unit, irfunc, irm));
            }

            Function *target = nullptr;

            if (strcmp("__debugbreak", call->call_target_name) == 0) {
                target = irm->debugbreak;
            } else {
                for (auto func : unit->functions) {
                    if (strcmp(call->call_target_name, func->name.data) == 0) {
                        target = func;
                        break;
                    }
                }
            }

            assert(target);

            return irm->insert_call(target, args);
        }

        case AST::VARIABLE: {
            auto var = static_cast<AST::Variable *>(expr);

            var->storage = irm->insert_alloca(var->expr_type);

            irm->insert_store(emit_expression(var->initializer, function, unit, irfunc, irm), var->storage);
            return nullptr;
        }

        case AST::IDENTIFIER: {
            auto ident = static_cast<AST::Identifier *>(expr);

            AST::Variable *target = nullptr;
            for (auto stmt : function->body->statements) {
                if (stmt->type == AST::VARIABLE) {
                    auto var = static_cast<AST::Variable *>(stmt);
                    if (strcmp(ident->name, var->name) == 0) {
                        target = var;
                        break;
                    }
                }
            }

            if (!target) {
                for (auto var : function->arguments) {
                    if (strcmp(ident->name, var->name) == 0) {
                        // parameters are read-only values
                        return var->storage;
                    }
                }
            }

            if (!target) {
                printf("No varibale named '%s' in function '%s'.\n", ident->name, function->name);
                exit(-1);
                return nullptr;
            }

            if (!target->storage) {
                printf("Variable '%s' used before declared!\n", target->name);
                exit(-1);
                return nullptr;
            }

            if (ident->is_lvalue_use) return target->storage;
            else                      return irm->insert_load(target->storage);
        }

        case AST::RETURN: {
            auto ret = static_cast<AST::Return *>(expr);

            if (ret->return_value) return irm->insert_return(emit_expression(ret->return_value, function, unit, irfunc, irm));
            return irm->insert_return();
        }

        case AST::WHILE: {
            auto wh = static_cast<AST::While *>(expr);

            auto loop_header = new Basic_Block();
            irfunc->insert(loop_header);

            auto loop_body = new Basic_Block();
            irfunc->insert(loop_body);

            auto loop_exit = new Basic_Block();

            irm->insert_branch(loop_header);

            irm->set_block(loop_header);
            auto cond = emit_expression(wh->condition, function, unit, irfunc, irm);
            irm->insert_branch(cond, loop_body, loop_exit);

            irm->set_block(loop_body);
            emit_scope(wh->body, function, unit, irfunc, irm);
            irm->insert_branch(loop_header);

            irfunc->insert(loop_exit);
            irm->set_block(loop_exit);

            return nullptr;
        }
    }
}

void emit_scope(AST::Scope *scope, AST::Function *function, Compilation_Unit *unit, Function *irfunc, IR_Man *irm) {
    for (auto expr: scope->statements) {
        emit_expression(expr, function, unit, irfunc, irm);
    }
}

int main(int argc, char **argv) {
    char *filename = nullptr;
    bool do_jit = false;

    for (int i = 1; i < argc; ++i) {
        char *option = argv[i];

        if (option[0] == '-') {
            if (strcmp("-jit", option) == 0) {
                do_jit = true;
                continue;
            } 

            printf("Unknown option: %s\n", option);
            return -1;
        } else {
            filename = option;
        }
    }

    if (!filename) {
        printf("Usage: %s <filename>\n", argv[0]);
        return -1;
    }

    char *source_text = get_file_contents(argv[1]);
    if (!source_text) {
        printf("File does not exist: %s\n", argv[1]);
        return -1;
    }

    Lexer lex;
    lex.buffer = source_text;

    Parser parser;
    lex.tokenize_text(&parser.tokens);

    /*
    for (auto t : parser.tokens) {
        if (t.type < Token::END) printf("Token: %c\n", t.type);
        else if (t.type == Token::IDENTIFIER) printf("Identifier: %s\n", t.string_value);
        else if (t.type >= Token::KEYWORD_START && t.type <= Token::KEYWORD_END) printf("Keyword: %s\n", keywords[t.type - Token::KEYWORD_START]);
        else if (t.type == Token::STRING) printf("String: \"%s\"\n", t.string_value);
    }
    */

    std::vector<AST::Function *> functions;
    while (parser.peek_token().type == Token::KEYWORD_FUNC) {
        AST::Function *function = parser.parse_function();
        functions.push_back(function);
    }

    Compilation_Unit unit;
    unit.target = get_host_target();

    IR_Man *irm = new IR_Man();
    Function *debugbreak = new Function();
    debugbreak->intrinsic_id = Function::DEBUG_BREAK;
    debugbreak->value_type = make_func_type(make_void_type());

    irm->debugbreak = debugbreak;

    for (auto function : functions) {
        Function *func = new Function();
        func->name = function->name;
        func->value_type = make_func_type(function->return_type);
        unit.functions.add(func);

        for (auto var : function->arguments) {
            Argument *arg = make_arg(var->expr_type);
            var->storage = arg;
            func->arguments.add(arg);
        }

        if (function->body) {
            Basic_Block *block = new Basic_Block();
            func->insert(block);

            irm->set_block(block);

            for (auto expr: function->body->statements) {
                emit_expression(expr, function, &unit, func, irm);
            }

            if (!irm->block->has_terminator()) irm->insert_return();
        }
    }

    if   (do_jit) do_jit_and_run_program_main(&unit);
    else          emit_obj_file(&unit);
    
    return 0;
}

