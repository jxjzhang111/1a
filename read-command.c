// UCLA CS 111 Lab 1 command reading

#include "command.h"
#include "command-internals.h"
#include "alloc.h"
#include <string.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#define DEBUG 0
#define STRMIN 8


// typedefs
typedef struct command_node {
	command_t command;
	struct command_node *next;
} command_node;

typedef struct command_stream {
	command_node *commands;
} command_stream;

typedef struct token {
    enum command_type type;
    int is_operator; // set to 0 for normal words
    char *word; // only populated for command_type = SIMPLE
    int line;
    struct token *next;
    struct token *prev;
} token;

// 1 token_stream per complete command
typedef struct token_stream {
    token *item;
    struct token_stream *next;
} token_stream;

/**** function declarations ****/

// constructors
command_stream_t init_command_stream ();
command_node *init_command_node ();
command_t init_command ();

// validation functions
int valid_char (int c);
int simple_char (int c);
int operator_char (int c);
int whitespace_char (int c);

// returns numeric priority of command_type
int precedence (enum command_type type);

// processing functions
command_node *single_command (int (*get_next_byte) (void *), void *get_next_byte_argument, int subshell);
void process_command (token **operators, command_node **commands, int prec, int *command_num, int *operator_num);
command_node *parse_command (token_stream **ts, int subshell);

int line = 1;

command_stream_t make_command_stream (int (*get_next_byte) (void *), void *get_next_byte_argument)
{
	// cs_head = first command in file
	command_stream_t cs = init_command_stream ();
	command_node *cn_last = cs->commands;
    line = 1;
    
    // token stream parsing
    int c = get_next_byte(get_next_byte_argument);
    int next = 1, in_comment = 0, paren = 0;
    token_stream *root_ts = NULL;
    token_stream *last_ts = root_ts;
    token_stream *current_ts = last_ts;
    token *last_t = NULL;
    
    while (c != EOF) {
        if (!valid_char (c)) {
            // throw error for unsupported characters
            if (!in_comment)
                error (1, 0, "%i: encountered unsupported character %c\n", line, c);
        } else if (!current_ts) {
            if (DEBUG) printf("%i: New command\n", line);
            paren = 0;
            current_ts = (token_stream *) checked_malloc (sizeof (token_stream));
            current_ts->item = NULL;
            current_ts->next = NULL;
            if (!root_ts)
                root_ts = current_ts;
            if (last_ts)
                last_ts->next = current_ts;
            last_ts = current_ts;
            
            last_t = NULL;
        }
        token *t = (token *) checked_malloc (sizeof (token));
        t->next = NULL; t->prev = NULL;
        t->line = line;
        
        if (c == '\n') {
            // newline logic -- check whether to start new token stream
            line++;
            in_comment = 0; // exit comment mode
            
            if (last_t && paren == 0
                && (!last_t->is_operator
                    || last_t->type == SEQUENCE_COMMAND
                    || !strcmp(last_t->word, ")"))) {
                // TODO: does a command end with a semicolon?
                if (last_t->type == SEQUENCE_COMMAND) {
                    // pop off last operator if it's a semicolon
                    token *temp = last_t;
                    last_t = last_t->prev;
                    last_t->next = NULL;
                    free (temp);
                }
                current_ts = NULL;
            } else if (last_t && last_t->is_operator && last_t->type == SIMPLE_COMMAND)
                error (1, 0, "%i: Newline after redirect %s is not permitted\n", t->line, last_t->word);
        } else if (in_comment || whitespace_char (c)) {
            // do nothing
        } else if (simple_char (c)) {
            char *word = (char *) checked_malloc (sizeof (char) * STRMIN);
            size_t max_word_size = sizeof (char) * STRMIN;
            int word_size = 0;
            // build word
            do {
                word[word_size] = c;
                word_size++;
                if (word_size == (int) (max_word_size/sizeof (char))) { // expand word if necessary
                    word = checked_grow_alloc (word, &max_word_size);
                }
                c = get_next_byte (get_next_byte_argument);
            } while (simple_char (c));
            word[word_size] = 0;
            next = 0; // already called next char
            
            t->type = SIMPLE_COMMAND;
            t->word = word;
            t->is_operator = 0;
            if (DEBUG) printf("Parsed word %s with max word length %i\n", word, (int) max_word_size);
            
            // push token to current_ts
            if (last_t) {
                last_t->next = t;
                t->prev = last_t;
            } else {
                current_ts->item = t;
            }
            last_t = t;
            
        } else if (operator_char (c)) {
            // parse operator
            if (DEBUG) printf("Operator %c\n", c);
            enum command_type type;
            char *word = (char *) checked_malloc (sizeof (char) * 1);
            word[0] = c;
            switch (c) {
                case ';':
                    type = SEQUENCE_COMMAND;
                    break;
                case '&':
                    c = get_next_byte (get_next_byte_argument);
                    if (c == '&')
                        type = AND_COMMAND;
                    else {
                        error (1, 0, "%i: syntax error on single &\n", line);
                    } // single & is a syntax error
                    break;
                    
                case '|':
                    c = get_next_byte (get_next_byte_argument);
                    if (DEBUG) printf("Char after |: %c\t", c);
                    if (c == '|') {
                        type = OR_COMMAND;
                        if (DEBUG) printf("Parsed to OR\n");
                    } else {
                        type = PIPE_COMMAND;
                        next = 0;
                        if (DEBUG) printf("Parsed to PIPE\n");
                    }
                    break;
                case '(':
                    type = SUBSHELL_COMMAND;
                    paren++;
                    break;
                    
                case ')':
                    type = SUBSHELL_COMMAND;
                    paren--;
                    if (last_t && last_t->type == SEQUENCE_COMMAND) {
                        // pop off last operator if it's a semicolon
                        token *temp = last_t;
                        last_t = last_t->prev;
                        last_t->next = NULL;
                        free (temp);
                    }
                    break;
                    
                case '<':
                    type = SIMPLE_COMMAND;
                    break;
                    
                case '>':
                    type = SIMPLE_COMMAND;
                    break;
            }

            t->type = type;
            t->word = word;
            t->is_operator = 1;
            
            // push token to current_ts
            if (last_t) {
                last_t->next = t;
                t->prev = last_t;
            } else {
                if (type != SUBSHELL_COMMAND)
                    error (1, 0, "%i: First item in command cannot be %s\n", t->line, word);
                current_ts->item = t;
            }
            last_t = t;
            
        } else if (c == '#') {
            // enter comment mode
            in_comment = 1;
        }
        
        if (next)
            c = get_next_byte(get_next_byte_argument);
        next = 1;
    }
    
    // command parsing
    token_stream *ts = root_ts;
    int i = 1;
    while (ts && ts->item) {
        if (DEBUG) printf("%i: \n", i);
        
		command_node *new_command = parse_command (&ts, 0);
		if (!cs->commands && new_command) {
			cs->commands = new_command;
			cn_last = new_command;
		} else if (new_command && cn_last) {
			cn_last->next = new_command;
			cn_last = new_command;
		}
        
        if (DEBUG) printf("New separate command added %i\n", i);
        token_stream *o_ts = ts;
        ts = ts->next;
        free (o_ts);
        i++;
    }

	if (DEBUG) printf ("EOF\n");
	return cs;
}

command_t read_command_stream (command_stream_t s)
{
	command_t c = NULL;
	if (s->commands) {
		if (DEBUG) printf("%p\n", s->commands);
		c = s->commands->command;
		command_node *old = s->commands;
		s->commands = s->commands->next;
		free (old);
	}
	return c;
}

command_node *parse_command (token_stream **ts, int subshell) {
    if (DEBUG && subshell) printf("Entering parse_command for subshell\n");
	command_node *commands = NULL; int command_num = 0; // command stack + counter
	token *operators = NULL; int operator_num = 0; // operator stack + counter
    
    token *t = (*ts)->item;
    int line;
    while (t) {
        if (DEBUG) printf("Processing token %s\n", t->word);
        if (t->is_operator) {
            token *current_op = (token *) malloc (sizeof (token));
            *current_op = *t;
            
            // process stacks if precedence is lower/equal than top of stack
            if (t->type == SUBSHELL_COMMAND) { // special case for SUBSHELL_COMMAND
                if (DEBUG) printf ("Processing subshell command %s\n", t->word);
                if (!strcmp(t->word, "(")) {
                    command_node *subshell_cn = init_command_node ();
                    subshell_cn->command->type = SUBSHELL_COMMAND;
                    
                    token_stream *sub_ts = (token_stream *) checked_malloc (sizeof (token_stream));
                    sub_ts->next = NULL;
                    sub_ts->item = t->next;
                    
                    command_node *child = parse_command (&sub_ts, 1);
                    t = sub_ts->item;
                    if (DEBUG) printf("After subshell, setting item to %s\n", t->word);
                    
                    subshell_cn->command->u.subshell_command = child->command;
                    free (child);
                    free (sub_ts);
                    
                    subshell_cn->next = commands; command_num++;
                    commands = subshell_cn;
                    
                } else { // Close out subshell
                    if (subshell == 0)
                        error (1, 0, "%i: encountered unexpected subshell close\n", t->line);
                    else {
                        if (operator_num + 1 == command_num)
                            process_command (&operators, &commands, 10, &command_num, &operator_num); // TODO: 10?
                        else {
                            error (1, 0, "%i: Incomplete command inside subshell\t %i operators, %i commands\n", t->line, operator_num, command_num);
                        }
                        // Add to queue if operators is empty and commands only has 1 item
                        if (operator_num == 0 && command_num == 1) {
                            (*ts)->item = t;
                            if (DEBUG) printf("Setting parent item to %s\n", t->word);
                            return commands;
                        } else
                            error (1, 0, "%i: Incomplete command inside subshell after processing\n", t->line);
                    }
                }
            } else if (!operators || (precedence (t->type) < precedence (operators->type))) {
                // push onto operators stack
                if (DEBUG) printf("High precedence operator encountered\t %i commands %i operators\n", command_num, operator_num);
                current_op->next = operators; operator_num++;
                operators = current_op;
            } else {
                if (DEBUG) printf("Lower or equal precedence operator encountered\t %i commands %i operators\n", command_num, operator_num);
                process_command (&operators, &commands, precedence (t->type), &command_num, &operator_num);
                // push onto operators stack
                current_op->next = operators; operator_num++;
                operators = current_op;
            }
            if (operator_num > command_num)
                error (1, 0, "%i: unexpected operator %i\n", t->line, t->type);
        } else {
            // build char ** word list
            // build SIMPLE_COMMAND and push to top of stack
            command_node *simple_cn = init_command_node ();
            simple_cn->command->type = SIMPLE_COMMAND;
            token *word = t;
            int word_count = 0;
            
            while (word && word->is_operator == 0) {
                word_count++;
                word = word->next;
            }
            
            if (DEBUG) printf("allocating %i words \n", word_count);
            simple_cn->command->u.word = (char **) checked_malloc (sizeof (char *) * (word_count + 1));
            int i = 0;
            while (i < word_count) {
                simple_cn->command->u.word[i] = t->word;
                i++;
                if (i < word_count)
                    t = t->next;
                else
                    simple_cn->command->u.word[i] = 0;
            }
            
            // push to commands stack
            simple_cn->next = commands; command_num++;
            commands = simple_cn;
        }
        token *old = t;
        t = t->next;
        line = old->line;
        free (old);

        if (DEBUG) printf("Processed token: command_num %i; operator_num %i\n", command_num, operator_num);
    }
    // process stacks
	process_command (&operators, &commands, 10, &command_num, &operator_num);

    // if last operator is a semi-colon, ignore
    if (operator_num == 1 && operators->type == SEQUENCE_COMMAND) {
        operator_num--;
        free (operators);
    }

	if (command_num != 1 || operator_num != 0)
		error (1, 0, "%i: Incomplete command at end of file\n", line);
	else if (subshell == 1)
		error (1, 0, "%i: Expecting )\n", line);
    return commands;
}

command_stream_t init_command_stream () {
	command_stream_t cs = (command_stream_t) checked_malloc (sizeof (command_stream));
	cs->commands = 0;
	return cs;
}

command_node *init_command_node () {
	command_node *c = (command_node *) checked_malloc (sizeof (command_node));
	c->next = NULL;
	c->command = init_command ();
	return c;
}

command_t init_command () {
	command_t c = (command_t) checked_malloc (sizeof (struct command));
	c->input = 0;
	c->output = 0;
	return c;
}

void process_command (token **operators, command_node **commands, int prec, int *command_num, int *operator_num) {
	if (DEBUG) printf("process_command %i;\t command_num %i; operator_num %i\n", prec, *command_num, *operator_num);
	command_node *cn_current = NULL;
	token *op_current = *operators;
	while (*operator_num > 0 && (prec >= (precedence(op_current->type)))) {
		if (DEBUG) printf("Processing operator %i with %i on command stack\n", op_current->type, *command_num);
		// pop off operators and merge items from the commands stack
		// operators should be empty, with 1 remaining command item
		if (*command_num < 2) {
			if (DEBUG) printf("%i: Insufficient [%i] commands available to build for operator %i\n", line, *command_num, op_current->type);
			break;
		}
		if (op_current->type == SIMPLE_COMMAND) { // Redirections
			// top command should be single word input/output (discard)
			// next command should be the object required (leave on top of stack)
			cn_current = *commands;
            
			*commands = cn_current->next; (*command_num)--;
			char **w = cn_current->command->u.word;
                              
			if (!strcmp(op_current->word,">")) {
                if ((*commands)->command->output) // TODO: Check? works in bash
                    error (1, 0, "%i: multiple output redirects in a row\n", op_current->line);
				(*commands)->command->output = *w;
			} else if (!strcmp(op_current->word,"<")) {
                if ((*commands)->command->output) // TODO: Check? works in bash
                    error (1, 0, "%i: input redirect cannot immediately follow output redirect\n", op_current->line);
                if ((*commands)->command->input) // TODO: Check? works in bash
                    error (1, 0, "%i: multiple input redirects in a row\n", op_current->line);
				(*commands)->command->input = *w;
			} else {
				error (1, 0, "%i: expected redirection %s\n", op_current->line, op_current->word);
			}
            if (DEBUG) printf("Used word %s\n", *w);
			if (*++w) // Check that there is only 1 word in cn_current
				error (1, 0, "%i: run-on word after redirection [%s]\n", op_current->line, *w);
			free (cn_current);
		} else { // create bifurcated command from top of operator stack
			command_node *tree_command = init_command_node ();
			if (!(*commands)) error (1, 0, "%i: missing arguments to bifurcated command %s\n", op_current->line, op_current->word);
			tree_command->command->u.command[1] = (*commands)->command;
			*commands = (*commands)->next; (*command_num)--;
			if (!(*commands)) error (1, 0, "%i: missing argument to bifurcated command %s\n", op_current->line, op_current->word);
			tree_command->command->u.command[0] = (*commands)->command;
			*commands = (*commands)->next; (*command_num)--;
			tree_command->command->type = op_current->type;

			// push to commands stack
			tree_command->next = *commands;
			*commands = tree_command; (*command_num)++;
		}
		*operators = (*operators)->next; (*operator_num)--;
		free (op_current);
		op_current = *operators;
	}
	if (DEBUG) printf("After processing: command_num %i; operator_num %i\n", *command_num, *operator_num);
}

int valid_char (int c) {
	if (simple_char (c) ||
			(c == '#') ||
			(c == '\n') ||
			operator_char (c) ||
			whitespace_char (c) 
	) return 1;
	return 0;
}

int simple_char (int c) {
	if ((c >= '0' && c <= '9') ||
			(c >= 'A' && c <= 'Z') ||
			(c >= 'a' && c <= 'z') ||
			(c == '!') ||
			(c == '%') ||
			(c == '+') ||
			(c == ',') ||
			(c == '-') || 
			(c == '.') ||
			(c == '/') ||
			(c == ':') || 
			(c == '@') || 
			(c == '^') ||
			(c == '_')
	) return 1;
	return 0;
}

int operator_char (int c) {
	if ((c == ';') ||
			(c == '&') ||
			(c == '|') ||
			(c == '(') ||
			(c == ')') ||
			(c == '<') ||
			(c == '>'))
		return 1;
	return 0;
}

int whitespace_char (int c) { // inline only
	if ((c == ' ') || (c == '\t'))
		return 1;
	return 0;
}

int precedence (enum command_type type) {
	switch (type) {
		case AND_COMMAND:         // A && B
			return 4;
		case SEQUENCE_COMMAND:    // A ; B
			return 5;
		case OR_COMMAND:          // A || B
			return 4;
		case PIPE_COMMAND:        // A | B
			return 3;
		case SIMPLE_COMMAND:      // a simple command <>
			return 2;
		case SUBSHELL_COMMAND:    // ( A )
			return 1;
	}
	return 0;
}

