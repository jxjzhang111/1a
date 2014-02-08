// UCLA CS 111 Lab 1 main program

#include <errno.h>
#include <error.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "command.h"
#include "command-internals.h"
#include "alloc.h"
#include <string.h>

#define DEBUG 0
#define WORDMIN 2

static char const *program_name;
static char const *script_name;

static void
usage (void)
{
    error (1, 0, "usage: %s [-pt] SCRIPT-FILE", program_name);
}

static int
get_next_byte (void *stream)
{
    return getc (stream);
}

// typedefs
typedef struct graph_node {
    command_t command;
    int seq_no;
    char **inputs;
    char **outputs;
    struct graph_node **out_edges;
    int max_edge_count;
    int edge_count;
    int in_edges;
    char executing;
} graph_node;

typedef struct graph_nodes {
    graph_node *node;
    struct graph_nodes *next;
} graph_nodes;

typedef struct graph_list {
    graph_nodes *graph;
    struct graph_list *next;
} graph_list;

typedef struct child_node {
    pid_t child;
    graph_node *node;
    struct child_node *next;
} child_node;

// functions
graph_node *parse_io (command_t command, int command_number);
char **extract_io (command_t command, char io);
int contains (char *w, char **words);
int intersect (char **words1, char **words2);
void add_edge (graph_node *src, graph_node *dst);
command_t execute_parallel (graph_nodes *node_list, int time_travel);
void decrement (graph_node *node);

int
main (int argc, char **argv)
{
    int opt;
    int command_number = 1;
    int print_tree = 0;
    int time_travel = 0;
    program_name = argv[0];

    for (;;)
        switch (getopt (argc, argv, "pt"))
            {
            case 'p': print_tree = 1; break;
            case 't': time_travel = 1; break;
            default: usage (); break;
            case -1: goto options_exhausted;
            }
    options_exhausted:;

    // There must be exactly one file argument.
    if (optind != argc - 1)
        usage ();

    script_name = argv[optind];
    FILE *script_stream = fopen (script_name, "r");
    if (! script_stream)
        error (1, errno, "%s: cannot open", script_name);
    command_stream_t command_stream =
        make_command_stream (get_next_byte, script_stream);

    command_t last_command = NULL;
    command_t command;

    if (print_tree || !time_travel) {
        while ((command = read_command_stream (command_stream))) {
            if (print_tree) {
                printf ("# %d\n", command_number++);
                print_command (command);
            } else {
                last_command = command;
                execute_command (command, time_travel);
            }
        }
    } else {
        if (DEBUG) printf("Commencing time travel\n");
        graph_nodes *node_list = (graph_nodes *) checked_malloc (sizeof (graph_nodes));
        graph_nodes *last_node = node_list;
        node_list->node = NULL;
        
        // Parse out input/outputs; create list of graph_nodes (node_list)
        while ((command = read_command_stream (command_stream))) {
            if (DEBUG) {
                printf ("# %d\n", command_number);
                print_command (command);
            }
            graph_node *n = parse_io (command, command_number++);
            if (last_node->node) {
                last_node->next = (graph_nodes *) checked_malloc (sizeof (graph_nodes));
                last_node = last_node->next;
            }
            last_node->node = n;
            last_node->next = NULL;
        }
        
        // Fill out dependency edges in node_list
        last_node = node_list;
        graph_nodes *prev_node = node_list;
        while (last_node) {
            while (prev_node->node->seq_no < last_node->node->seq_no) {
                if (intersect (last_node->node->outputs, prev_node->node->inputs)
                    || intersect (last_node->node->inputs, prev_node->node->outputs)) {
                    add_edge (prev_node->node, last_node->node);
                }
                prev_node = prev_node->next;
            }
            prev_node = node_list;
            last_node = last_node->next;
        }
        
        // Execute the graph_nodes
        last_command = execute_parallel (node_list, time_travel);
    }

    return print_tree || !last_command ? 0 : command_status (last_command);
}

// Allocates a graph_node instance that points to command and holds dependency info
graph_node *parse_io (command_t command, int command_number) {
    graph_node *node = (graph_node *) checked_malloc (sizeof (graph_node));
    node->command = command;
    node->seq_no = command_number;
    node->outputs = extract_io (command, 'o');
    node->inputs = extract_io (command, 'i');
    node->in_edges = 0;
    node->max_edge_count = 0;
    node->edge_count = 0;
    node->out_edges = NULL;
    node->executing = 0;

    if (DEBUG) {
        printf ("\n\toutputs: ");
        char **w = node->outputs;
        while (*w) {
            printf("%s ", *w);
            *w++;
        }
        printf ("\n\tinputs: ");
        w = node->inputs;
        while (*w) {
            printf("%s ", *w);
            *w++;
        }
        printf ("\n");
    }
    return node;
}

// Returns list of words from command that are classified as input/output ('i' or 'o')
char **extract_io (command_t command, char io) {
    size_t max_word_count = WORDMIN;
    size_t max_size = max_word_count * sizeof (char *);
    size_t word_count = 0;
    char **words = (char **) checked_malloc (max_size);
    char **words1 = NULL, **words2 = NULL;
    
    if (command->type == SIMPLE_COMMAND) {
        if (io == 'o') {
            if (command->output) {
                words[word_count] = command->output;
                word_count++;
            }
        } else if (io == 'i') {
            if (command->input) {
                words[word_count] = command->input;
                word_count++;
            }
            
            char **w = command->u.word;
            while (*++w && *w[0] != '-' && !contains(*w, words)) {
                words[word_count] = *w;
                word_count++;
                if (word_count == max_word_count) {
                    words = checked_grow_alloc (words, &max_size);
                    max_word_count = max_size / (sizeof (char *));
                }
            }
        }
    } else if (command->type == SUBSHELL_COMMAND) { // Note: not supporting redirects after subshells
        words1 = extract_io (command->u.subshell_command, io);
    } else {
        words1 = extract_io (command->u.command[0], io);
        words2 = extract_io (command->u.command[1], io);
    }
    
    // append words1/2 to words while skipping repeats
    char **w;
    if (words1) {
        w = words1;
        while (*w) {
            if (!contains(*w, words)) {
                words[word_count] = *w;
                word_count++;
                if (word_count == max_word_count) {
                    words = checked_grow_alloc (words, &max_size);
                    max_word_count = max_size / (sizeof (char *));
                }
            }
            *w++;
        }
        free (words1);
    }
    words[word_count] = 0;

    if (words2) {
        w = words2;
    
        while (*w) {
            if (!contains(*w, words)) {
                words[word_count] = *w;
                word_count++;
                if (word_count == max_word_count) {
                    words = checked_grow_alloc (words, &max_size);
                    max_word_count = max_size / (sizeof (char *));
                }
            }
            *w++;
        }
        free (words2);
    }
    words[word_count] = 0;
    return words;
}

// Returns 1 if words1 and words2 intersect
int intersect (char **words1, char **words2) {
    while (*words1) {
        if (DEBUG) printf ("\tIntersect: %s\n", *words1);
        if (contains (*words1, words2)) {
            return 1;
        }
        *words1++;
    }
    return 0;
}

// Returns 1 if words contains w
int contains (char *w, char **words) {
    while (*words) {
        if (DEBUG) printf ("\t\tContains: %s %s\n", w, *words);
        if (!strcmp (w, *words)) {
            return 1;
        }
        *words++;
    }
    return 0;
}

void add_edge (graph_node *src, graph_node *dst) {
    if (DEBUG) printf ("Adding edge from %i to %i\n", src->seq_no, dst->seq_no);
    if (!src->out_edges) {
        src->max_edge_count = WORDMIN;
        src->out_edges = (graph_node **) checked_malloc (sizeof (graph_node *) * src->max_edge_count);
    }
    src->out_edges[src->edge_count] = dst;
    src->edge_count++;
    if (src->edge_count == src->max_edge_count) {
        size_t max_size = src->max_edge_count * sizeof (graph_node *);
        src->out_edges = checked_grow_alloc (src->out_edges, &max_size);
        src->max_edge_count = max_size / (sizeof (graph_node *));
    }
    src->out_edges[src->edge_count] = 0;
    dst->in_edges++;
}

command_t execute_parallel (graph_nodes *node_list, int time_travel) {
    command_t last_command = node_list->node->command;
    graph_nodes *current_node = node_list;
    child_node *children = NULL;
    child_node *last_child = children;
    pid_t child;
    int status;

    // Find disconnected graphs and separate into graphs
    // Execute each graph separately (fork)
    // Grandparent: wait for all graphs to complete
    
    while (node_list) {
        // Parent: find nodes with no incoming edges and run separately (fork)
        while (current_node) {
            if (DEBUG) printf ("Pre-execution: %i (%i:%i)\n", current_node->node->seq_no, current_node->node->in_edges, current_node->node->executing);
            if (current_node->node->in_edges == 0 && !current_node->node->executing) {
                child = fork();
                if (child == 0) { // child
                    if (DEBUG) printf ("Executing command %i... ", current_node->node->seq_no);
                    execute_command (current_node->node->command, time_travel);
                    if (DEBUG) printf ("complete [%i]\n", current_node->node->command->status);
                    exit(current_node->node->command->status);
                } else if (child > 0) { // parent
                    current_node->node->executing = 1;
                    // append new child process to children
                    child_node *new_child = (child_node *) malloc (sizeof (child_node *));
                    new_child->node = current_node->node;
                    new_child->child = child;
                    new_child->next = NULL;
                    
                    if (last_child)
                        last_child->next = new_child;
                    last_child = new_child;
                    
                    if (!children)
                        children = last_child;
                } else
                    error (1, 0, "execute_parallel: failed to create child process!");
            }
            current_node = current_node->next;
        }
        
        if (DEBUG) printf ("Traversed! ");
        
        // Parent waitpid for children
        if (DEBUG) printf("\nWaiting for %i:%i to complete...", children->node->seq_no, children->child);
        waitpid (children->child, &status, 0);
        if (DEBUG) printf(" completed with status %i\n", status);
        child_node *completed_child = children;
        int complete = children->node->seq_no;
        
        
        
        // Parent: prune completed child nodes from nodelist; decrement in_edges
        children = children->next;
        current_node = node_list;
        if (node_list->node->seq_no == complete) { // special case for if the completed node is first in nodelist
            node_list = node_list->next;
            decrement (current_node->node);
            free (current_node->node);
            free (current_node);
        } else if (current_node) {
            while (current_node->next) {
                if (current_node->next->node->seq_no == complete) {
                    graph_nodes *nodes_done = current_node->next;
                    current_node->next = nodes_done->next;
                    decrement (nodes_done->node);
                    free (nodes_done->node);
                    free (nodes_done);
                }
            }
        }
        if (!children)
            last_child = NULL;
        
        if (DEBUG) printf ("Pruned %i from node list\n", complete);
        free(completed_child);
        current_node = node_list;
        // TODO: (recursive) Find disconnected graphs, and execute separately
    }
    
    
    return last_command;
}

void decrement (graph_node *node) {
    graph_node **out = node->out_edges;
    while (out && *out) {
        if (DEBUG) printf ("\nDecrementing from %i; ", (*out)->seq_no);
        (*out)->in_edges--;
        *out++;
    }
}