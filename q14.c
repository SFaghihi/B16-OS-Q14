//
//  main.c
//  q14
//
//  Created by Soroush Faghihi on 2/13/18.
//  Copyright © 2018 soroush. All rights reserved.
//
#include "util.h"

void usage(const char *name)
{
    printf("Usage:\n");
    printf("Client mode: %s [-c] [-p port] [-e /stderr/file/path] SERVER COMMAND\n", name);
    printf("Server mode: %s -s [-p port] [BIND ADDRESS]\n", name);
    printf("Print Usage: %s -h\n", name);
}

int main (int argc, char **argv)
{
    // Default client behaviour
    int is_client = 1;
    // Default port: 8889
    char *port = "8889";
    // Default don't receive or record stderr in Client Mode
    char *stderr_fn = NULL;
    
    opterr = 0;
    
    int c;
    while ((c = getopt (argc, argv, "hscp:e:")) != -1)
        switch (c)
    {
        case 'h':
            usage(argv[0]); return 0;
            break;
        case 's':
            is_client = 0;
            break;
        case 'c':
            is_client = 1;
            break;
        case 'p':
            port = optarg;
            break;
        case 'e':
            stderr_fn = optarg;
            break;
        case '?':
            if (optopt == 'p' || optopt == 'e')
                fprintf (stderr, "Option -%c requires an argument.\n", optopt);
            else if (isprint (optopt))
                fprintf (stderr, "Unknown option `-%c'.\n", optopt);
            else
                fprintf (stderr,
                         "Unknown option character `\\x%x'.\n",
                         optopt);
            usage(argv[0]);
            return 1;
        default:
            abort();
    }
    
    /* Client routine */
    if (is_client) {
        if (argc - optind == 2)
            return client_routine(argv[argc - 2], port, argv[argc - 1], stderr_fn);
        // Need 2 Args
        fprintf(stderr, "Client mode requires two arguments: [SERVER IP] [COMMAND]\n");
        usage(argv[0]);
    }
    /* Server routine */
    else {
        // IP Address provided to bind to
        if (optind == argc - 1)
            return server_routine(argv[argc - 1], port);
        // Default IP Address bind: 0.0.0.0
        else if (optind == argc)
            return server_routine(NULL, port);
        // Too many Arguments!
        else
            fprintf(stderr, "Server mode requires one optional argument: [BIND IP ADDRESS]\n");
        usage(argv[0]);
    }
    return 1;
}
