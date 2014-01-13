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

typedef struct op_stack {
	enum command_type type;
	int op;
	struct op_stack *next;
} op_stack;

typedef struct op_stack *op_stack_t;

/**** function declarations ****/

// constructors
command_stream_t init_command_stream ();
command_node *init_command_node ();
command_t init_command ();
op_stack_t init_op_stack (enum command_type type);

// validation functions
int valid_char (int c);
int simple_char (int c);
int operator_char (int c);
int whitespace_char (int c);

// returns numeric priority of command_type
int precedence (enum command_type type);

// processing functions
void process_command (op_stack_t *operators, command_node **commands, int prec, int *command_num, int *operator_num);

int line = 1;

command_stream_t make_command_stream (int (*get_next_byte) (void *), void *get_next_byte_argument)
{
	// cs_head = first command in file
	command_stream_t cs = init_command_stream ();
	command_node *cn_last = cs->commands;
	command_node *cn_current = 0;
	
	// internal command parsing
	int in_comment = 0; // bool for in comment
	int newline = 0; // bool for whether prior char was newline
	command_node *commands = NULL; int command_num = 0; // command stack + counter
	op_stack_t operators = NULL; int operator_num = 0; // operator stack + counter

	int c = get_next_byte(get_next_byte_argument);
	while (c != EOF) { // Traverse input file
		if (c == '\n') { 
			if (DEBUG) printf("Newline encountered\n");
			newline = 1;
			line++;
			in_comment = 0; 

			if (operator_num + 1 == command_num) 
				process_command (&operators, &commands, 10, &command_num, &operator_num); // TODO: Replace 10 with something 
			else if (operator_num == command_num) {
				// Check that prior operator is not <>
				if (operators && operators->type == SIMPLE_COMMAND)
					error (1, 0, "%i: Newline after redirect %c is not permitted\n", line, operators->op);
			}
			// Add to queue if operators is empty and commands only has 1 item
			if (operator_num == 0 && command_num == 1) {
				if (!cs->commands && commands) {
					cs->commands = commands;
					cn_last = commands;
				} else if (commands && cn_last) {
					cn_last->next = commands;
					cn_last = commands;
				}
				commands = commands->next; command_num--;
			}

		} else if (c == '#') {
			in_comment = 1;
			newline = 0;
		}

		if (!in_comment) {
			if (!valid_char(c)) {
				// TODO: free allocated memory
				error (1, 0, "%i: encountered unsupported character %c\n", line, c);
			}

			if (operator_char (c)) {
				if (DEBUG) printf("Operator %c\n", c);
				enum command_type type;
				switch (c) {
					case ';':
						type = SEQUENCE_COMMAND;
						break;

					case '&':
						c = get_next_byte (get_next_byte_argument);
						if (c == '&')
							type = AND_COMMAND;
						else {
							ungetc (c, get_next_byte_argument); 
							error (1, 0, "%i: syntax error on single %c\n", line, c);
						} // TODO: single & is a syntax error
						break;

					case '|':
						c = get_next_byte (get_next_byte_argument);
						if (c == '|')
							type = OR_COMMAND;
						else {
							type = PIPE_COMMAND;
							ungetc (c, get_next_byte_argument); 
						} 
						break;

					case '(':
						type = SUBSHELL_COMMAND;
						break;

					case ')': // will handle separately for SUBSHELL_COMMAND
						error (1, 0, "%i: encountered unexpected subshell close %c\n", line, c);
						break;

					case '<':
						type = SIMPLE_COMMAND;
						break;

					case '>':
						type = SIMPLE_COMMAND;
						break;
				}
				op_stack_t current_op = init_op_stack (type);
				if (newline && type != SUBSHELL_COMMAND) {
					error (1, 0, "%i: encountered an unexpected token %i after a newline\n", line, type);
				}
				newline = 0;

				if (type == SIMPLE_COMMAND) {
					current_op->op = c;
				}
				
				if (type == SUBSHELL_COMMAND) { // special case for SUBSHELL_COMMAND
					// TODO: currently creates a long SIMPLE_COMMAND until finding the close paren
					// nest in a SUBSHELL_COMMAND and push onto commands stack

					// SUBSHELL specific
					int close_paren = 1; // break when counter hits 0					
					command_node *subshell_cn = init_command_node ();
					subshell_cn->command->type = SUBSHELL_COMMAND;	
					c = get_next_byte (get_next_byte_argument);
					if (c == ')')
						close_paren--;
					else if (c == '(')
						close_paren++;

					// Create SIMPLE_COMMAND
					int word_count = 0, char_count = 0, max_word_count = 0, max_char_count = 0, max_word_size = 0;
					char *current_word;

					command_node *simple_cn = init_command_node ();
					simple_cn->command->type = SIMPLE_COMMAND;
					simple_cn->command->u.word = (char **) checked_malloc (sizeof (char *) * STRMIN);
					max_word_count = STRMIN; word_count = 0; max_word_size = sizeof (char *) * STRMIN;

					// do not push simple_cn to commands stack

					do {
						// initiate new word
						current_word = (char *) checked_malloc (sizeof (char) * STRMIN); 
						max_char_count = STRMIN; char_count = 0;

						// populate current_word
						while (close_paren > 0 && valid_char (c) && c != '#' && c != '\n' && c != EOF) {
							current_word[char_count] = c; 
							char_count++;
							if (char_count == max_char_count) { // expand current_word if necessary
								current_word = checked_grow_alloc (current_word, (size_t *) &max_char_count);
							}	
							c = get_next_byte (get_next_byte_argument);
							if (c == ')')
								close_paren--;
							else if (c == '(')
								close_paren++;
						}
						current_word[char_count] = 0; // NULL terminate current_word

						// append current_word to u.word
						simple_cn->command->u.word[word_count] = current_word; 
						word_count++;
						if (word_count == max_word_count) { // expand command->u.word
							simple_cn->command->u.word = (char **) checked_grow_alloc (simple_cn->command->u.word, (size_t *) &max_word_size);
						}
						simple_cn->command->u.word[word_count] = 0; // NULL terminate u.word

						// eat whitespace to next word
						while (whitespace_char (c) || c == '\n') {
							c = get_next_byte (get_next_byte_argument);
							if (c == ')')
								close_paren--;
							else if (c == '(')
								close_paren++;
						}	

						if (DEBUG) printf("%i: %s %c\n", word_count, current_word, c);
					} while (close_paren > 0 && c != '#' && c != EOF);
					ungetc (c, get_next_byte_argument); 
					if (close_paren > 0)
						error (1, 0, "%i: %i unclosed parentheses\n", line, close_paren);
					// fill out subshell_cn push onto commands stack
					subshell_cn->command->u.subshell_command = simple_cn->command;
					free(simple_cn);
					subshell_cn->next = commands; command_num++;
					commands = subshell_cn;
				} else if (!operators || (precedence (type) < precedence (operators->type))) {
					// push onto operators stack
					current_op->next = operators; operator_num++;
					operators = current_op;
				} else {
					process_command (&operators, &commands, precedence (type), &command_num, &operator_num);
					// push onto operators stack
					current_op->next = operators; operator_num++;
					operators = current_op;
				}
				if (operator_num > command_num)
					error (1, 0, "%i: unexpected operator %i\n", line, type);
			} else if (whitespace_char (c)) {
			} else if (simple_char (c)) { 
				newline = 0;
				// create new text simple_command object
				// TODO: create module for this to be reused by SUBSHELL_COMMAND
				int word_count = 0, char_count = 0, max_word_count = 0, max_char_count = 0, max_word_size = 0;
				char *current_word;

				command_node *simple_cn = init_command_node ();
				simple_cn->command->type = SIMPLE_COMMAND;
				simple_cn->command->u.word = (char **) checked_malloc (sizeof (char *) * STRMIN);
				max_word_count = STRMIN; word_count = 0; max_word_size = sizeof (char *) * STRMIN;

				// push to commands stack
				simple_cn->next = commands; command_num++;
				commands = simple_cn;

				do {
					// initiate new word
					current_word = (char *) checked_malloc (sizeof (char) * STRMIN); 
					max_char_count = STRMIN; char_count = 0;

					// populate current_word
					do {
						current_word[char_count] = c; 
						char_count++;
						if (char_count == max_char_count) { // expand current_word if necessary
							current_word = checked_grow_alloc (current_word, (size_t *) &max_char_count);
						}
						c = get_next_byte (get_next_byte_argument);
					} while (simple_char (c));
					current_word[char_count] = 0; // NULL terminate current_word

					// append current_word to u.word
					commands->command->u.word[word_count] = current_word; 
					word_count++;
					if (word_count == max_word_count) { // expand command->u.word
						commands->command->u.word = (char **) checked_grow_alloc (commands->command->u.word, (size_t *) &max_word_size);
					}
					commands->command->u.word[word_count] = 0; // NULL terminate u.word

					// eat whitespace to next word
					while (whitespace_char (c)) {
						c = get_next_byte (get_next_byte_argument);
					}	

					if (DEBUG) printf("%i: %s\n", word_count, current_word);
				} while (simple_char (c));
				ungetc (c, get_next_byte_argument); 
			}
		}

		c = get_next_byte (get_next_byte_argument);
	}

	process_command (&operators, &commands, 10, &command_num, &operator_num);
	if (command_num == 0 && operator_num == 0) // all whitespace? no content
		return cs;

	if (command_num != 1 || operator_num != 0)
		error (1, 0, "%i: Incomplete command at end of file\n", line);

	if (!cs->commands && commands) {
		cs->commands = commands;
		cn_last = commands;
	} else if (commands && cn_last) {
		cn_last->next = commands;
		cn_last = commands;
	}
	if (DEBUG) printf("State of cn_last %p\n", cn_last);
	cn_last->next = NULL;

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

op_stack_t init_op_stack (enum command_type type) {
	op_stack_t o = (op_stack_t) checked_malloc (sizeof (struct op_stack));
	o->type = type;
	o->op = 0;
	o->next= 0;
	return o;
}

// TODO: need some state management that stack is okay to process
void process_command (op_stack_t *operators, command_node **commands, int prec, int *command_num, int *operator_num) {
	if (DEBUG) printf("process_command %i;\t command_num %i; operator_num %i\n", prec, *command_num, *operator_num);
	command_node *cn_current = NULL;
	op_stack_t op_current = *operators;
	while (*operator_num > 0 && (prec >= (precedence(op_current->type)))) {
		if (DEBUG) printf("Processing operator %i\n", op_current->type);
		// pop off operators and merge items from the commands stack
		// operators should be empty, with 1 remaining command item
		if (op_current->type == SIMPLE_COMMAND) { // Redirections
			// top command should be single word input/output (discard)
			// next command should be the object required (leave on top of stack)
			cn_current = *commands;
			*commands = cn_current->next; (*command_num)--;
			char **w = cn_current->command->u.word;
			if (op_current->op == '>') {
				(*commands)->command->output = *w;
			} else if (op_current->op == '<') {
				(*commands)->command->input = *w;
			} else {
				error (1, 0, "%i: expected redirection %c\n", line, op_current->op);
			}
			free (cn_current);
			// TODO: check that there is only 1 word in cn_current
		} else { // create bifurcated command from top of operator stack
			if (*command_num < 2) {
				if (DEBUG) printf("%i: Insufficient [%i] commands available to build for operator %i\n", line, *command_num, op_current->op);
				break;
			}
			command_node *tree_command = init_command_node ();
			if (!(*commands)) error (1, 0, "%i: missing arguments to bifurcated command %i\n", line, op_current->op);
			tree_command->command->u.command[1] = (*commands)->command;
			*commands = (*commands)->next; (*command_num)--;
			if (!(*commands)) error (1, 0, "%i: missing argument to bifurcated command %i\n", line, op_current->op);
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
	if (DEBUG) printf("command_num %i; operator_num %i\n", *command_num, *operator_num);
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

