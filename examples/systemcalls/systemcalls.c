#include "systemcalls.h"

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{
    /*Return false if the command is NULL */
    if (cmd == NULL)
    {
        return false;
    }

    /*Execute the given shell command using system() */
    int status = system(cmd);
    if (status == -1)
    {
        /* Return false on shell invocation error */
        return false;
    }

    /* Check if child exited normally and returned 0 */
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        return true;
    }

    return false;
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

    va_end(args);

    pid_t pid = fork();
    if (pid == -1)
    {
        return false;
    }

    if (pid == 0)
    {
        /* Child: replace process image */
        execv(command[0], command);
        /* execv failed */
        _exit(EXIT_FAILURE);
    } 
    else
    {
        /* Parent: wait for child to finish */
        int status;
        if (waitpid(pid, &status, 0) < 0)
        {
            return false;
        }

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        {
            return true;
        }
        /* treat signal termination as failure */
        return false;
    }
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;


    va_end(args);

    int outfd = -1;
    if (outputfile)
    {
        outfd = open(outputfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (outfd < 0)
        {
            return false;
        }
        else
        {
            pid_t pid = fork();
            if (pid == -1)
            {
                close(outfd);
                return false;
            }

            if (pid == 0)
            {
                /* child */
                /* Redirect stdout to output file */
                if (dup2(outfd, STDOUT_FILENO) == -1)
                {
                    /* dup2 error */
                    close(outfd);
                    _exit(EXIT_FAILURE);
                }

                close(outfd);
                /* execute command */
                execv(command[0], command);
                /* execv failed */
                _exit(EXIT_FAILURE);
            } 
            else
            {
                /* parent */
                close(outfd);
                int status;
                /* wait for child to finish */
                if (waitpid(pid, &status, 0) == -1)
                {
                    /* waitpid error */
                    return false;
                }

                if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
                {
                    /* command executed successfully */
                    return true;
                }

                return false;
            }                
        }
    }
    else
    {
        /* outputfile is NULL */
        return false;
    }
}
