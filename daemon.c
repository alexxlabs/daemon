//----------------------------------------------------------------------------//
// -*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-  //
//                                                                            //
// daemon.c - A *nix daemon template/example.
//                                                                            //
// It currently supports:                                                     //
//  - forking to the background                                               //
//  - logging to syslog                                                       //
//  - parsing command-line arguments                                          //
//  - parsing a simplistic config file                                        //
//                                                                            //
// Core daemon functionality all goes into daemon_main, which is located just //
// above the main function.                                                   //
//                                                                            //
// *-,_,-*'^^'*-,_,-*'^^'*-,_,-*'^^'*-,_,-*'^^'*-,_,-*'^^'*-,_,-*'^^'*-,_,-*  //
//                                                                            //
// Copyright (c) 2018 c0d3st0rm                                               //
//                                                                            //
// Boost Software License - Version 1.0 - August 17th, 2003                   //
//                                                                            //
// Permission is hereby granted, free of charge, to any person or             //
// organization obtaining a copy of the software and accompanying             //
// documentation covered by this license (the "Software") to use, reproduce,  //
// display, distribute, execute, and transmit the Software, and to prepare    //
// derivative works of the Software, and to permit third-parties to whom the  //
// Software is furnished to do so, all subject to the following:              //
//                                                                            //
// The copyright notices in the Software and this entire statement, including //
// the above license grant, this restriction and the following disclaimer,    //
// must be included in all copies of the Software, in whole or in part, and   //
// all derivative works of the Software, unless such copies or derivative     //
// works are solely in the form of machine-executable object code generated   //
// by a source language processor.                                            //
//                                                                            //
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR //
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   //
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT  //
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE  //
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR           //
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE  //
// USE OR OTHER DEALINGS IN THE SOFTWARE.                                     //
//                                                                            //
// *-,_,-*'^^'*-,_,-*'^^'*-,_,-*'^^'*-,_,-*'^^'*-,_,-*'^^'*-,_,-*'^^'*-,_,-*  //
//----------------------------------------------------------------------------//

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

//----------------------------------------------------------------------------//
// -*'^'*-,__,-*'^'*-,__,-*' Macros and definitions '*-,__,-*'^'*-,__,-*'^'*- //
//----------------------------------------------------------------------------//

/// Daemon name string constant
#define DAEMON_NAME						"mydaemon"
/// Version string constant
#define PROGRAM_VERSION_STR				"0.1.0"
/// Whether to fork or not by default. 1 = run in foreground, 0 = daemonize/fork.
#define DEFAULT_RUN_IN_FOREGROUND		1
/// Default config file path. Specifying 0 here means no default config file.
#define DEFAULT_CONFIG_FILE_PATH		0
/// Default working directory to chdir() into.
#define DEFAULT_WORKING_DIR				"/"

/**
 * perror()-like macro which logs the error using syslog() instead.
 * Supports variadic arguments too.
 * @param s A description/indication of what happened/where the error
 * occurred. Must be a C string constant.
 */
#define perror_syslog(s, ...) syslog(LOG_ERR, s ": %s", ##__VA_ARGS__, strerror(errno))

// Helper macros for parse_config_file. These should not be used
// anywhere else.
#define isvalididentifier(c) (isalnum((c)) || (c) == '_' || (c) == '-')
#define isvalididentifierstart(c) (isalpha((c)) || (c) == '_')
#define iseol(c) ((c) == '\n')
#define skip_whitespace() \
		while (isspace(c)) { c = *cur++; }
#define skip_whitespace_not_eol() \
		while (isspace(c) && !iseol(c)) { c = *cur++; }
#define skip_until_eol() \
		while (c != '\0' && !iseol(c)) { c = *cur++; }
#define skip_until_eol_or_comment() \
		while (c != '\0' && !iseol(c) && c != '#') { c = *cur++; }
#define identifier_matched(value) \
		(!strncmp((const char*) (value), (const char*) id_tmp, (sizeof((value)) / sizeof((value)[0]))))
#define try_validate_boolean(dest) \
	do { \
		int val = validate_boolean((const char*) val_tmp); \
		if (val == 1) { \
			dest = 1; \
		} else if (val == 0) { \
			dest = 0; \
		} else { \
			/* invalid value */ \
			errno = EINVAL; \
			ret = 1; \
			goto end; \
		} \
	} while (0)

//----------------------------------------------------------------------------//
// -*'^'*-,__,-*'^'*-,__,-*' typedefs and structures *-,__,-*'^'*-,__,-*'^'*- //
//----------------------------------------------------------------------------//

/**
 * Structure which stores daemon options, such as those which might be
 * provided on the command-line or in a config file.
 */
typedef struct {
	/// Configuration file path.
	const char *config_file;
	/// Whether the daemon should fork to the background or not
	char background;
	/// Whether verbose logging should occur
	char verbose;
	/// syslog ident
	char syslog_ident[256];
} options_t;

//----------------------------------------------------------------------------//
// -*'^'*-,__,-*'^'*-,__,-*'^'* Global constants *'^'*-,__,-*'^'*-,__,-*'^'*- //
//----------------------------------------------------------------------------//

/// Global constant defining the daemon version
static const char *version_str = PROGRAM_VERSION_STR;

/// Global constant defining the daemon name. Using NULL for daemon_name
/// will be replaced by argv[0] at runtime
static const char *daemon_name = DAEMON_NAME;

/// Stuff for getopt_long.
static const struct option long_options[] = {
	{"help",		no_argument,		0,	'h'},
	{"version",		no_argument,		0,	'v'},
	{"verbose",		no_argument,		0,	'V'},
	{"daemonize",	no_argument,		0,	'd'},
	{"foreground",	no_argument,		0,	'f'},
	{"config",		required_argument,	0,	'c'},
	{"ident",		required_argument,	0,	'Z'},
	{0,				0,					0,	0}
};

/// More stuff for getopt_long.
static const char *short_options = "hvVdfc:Z:";

/// Short help message.
static const char *short_usage =
"[-h, --help] [-v, --version] [-V, --verbose]\n"
"    [-d, --daemonize] [-f, --foreground] [-c, --config <path>]\n"
"    [-Z, --ident <ident>]\n";

/// General help message for the above options
static const char *long_usage =
"Version v" PROGRAM_VERSION_STR "\n."
"Available options:\n"
" -h, --help           Show this help message.\n"
" -v, --version        Show this program's version.\n"
" -V, --verbose        Enable more verbose logging.\n"
#if (DEFAULT_RUN_IN_FOREGROUND == 1)
	" -d, --daemonize      Fork and run in the background.\n"
	" -f, --foreground     Run in the foreground (default).\n"
#else	
	" -d, --daemonize      Fork and run in the background (default).\n"
	" -f, --foreground     Run in the foreground.\n"
#endif
" -c, --config <path>  Use the specified config file.\n"
" -Z, --ident <str>    Use the specified string as the syslog ident.\n";

//----------------------------------------------------------------------------//
// -*'^'*-,__,-*'^'*-,__,-*'^'* Utility routines *'^'*-,__,-*'^'*-,__,-*'^'*- //
//----------------------------------------------------------------------------//

/**
 * Variadic-style perror()-like function.
 * @param fmt See printf(3).
 * @return The number of bytes written to stderr.
 */
static int vperror(const char *fmt, ...) {
	int ret;
	va_list ap;
	va_start(ap, fmt);
	ret = vfprintf(stderr, fmt, ap);
	va_end(ap);
	ret += fprintf(stderr, ": %s\n", strerror(errno));
	return ret;
}

/**
 * Reads the entire contents of the file pointed to by the path.
 * @param path The path of the file to read.
 * @return A pointer to the read data in an malloc()-ed buffer, or NULL
 * on error.
 */
static char *read_entire_file(const char *path) {
	FILE *f;
	void *buf;
	long fsiz;
	ssize_t ret, nread, remaining;
	
	if ((f = fopen(path, "r")) == NULL) {
		goto err;
	}
	
	fseek(f, 0, SEEK_END);
	fsiz = ftell(f);
	fseek(f, 0, SEEK_SET);
	
	buf = malloc((size_t) fsiz);
	if (buf == NULL) {
		goto err;
	}
	
	remaining = (ssize_t) fsiz;
	nread = 0;
	while (remaining > 0) {
		ret = fread(((uint8_t*) buf + nread), 1, remaining, f);
		if (ret <= 0) {
			goto err;
		}
		nread += ret;
		remaining -= ret;
	}
	
	fclose(f);
	return (char*) buf;
	
err:
	if (f != NULL) {
		fclose(f);
	}
	if (buf != NULL) {
		free(buf);
	}
	return NULL;
}

/**
 * Checks if the specfied file exists.
 * @param path The path of the file to check.
 * @return 1 if the file exists, 0 otherwise.
 */
static int file_exists(const char *path) {
	struct stat sb;
	return stat(path, &sb) == 0 ? 1 : 0;
}

/**
 * Helper function to validate a boolean string. Valid input is (case
 * insensitive):
 * true:  "y", "yes", "true", "1"
 * false: "n", "no", "false", "0"
 * @param str The string to validate.
 * @return 0 if the specified string evaluates to false, 1 if it
 * evaluates to true, and -1 if it's invalid.
 */
static int validate_boolean(const char *str) {
	size_t len = strlen(str);
	switch (len) {
		case 1: {
			char c = str[0];
			if (c == 'y' || c == 'Y' || c == '1') {
				return 1;
			} else if (c == 'n' || c == 'N' || c == '0') {
				return 0;
			}
			break;
		}
		case 2:
			if (tolower(str[0]) == 'n' && tolower(str[1]) == 'o') {
				return 0;
			}
			break;
		case 3:
			if (tolower(str[0]) == 'y' && tolower(str[1]) == 'e' && tolower(str[2]) == 's') {
				return 0;
			}
			break;
		case 4:
			if (tolower(str[0]) == 't' && tolower(str[1]) == 'r' && tolower(str[2]) == 'u' && tolower(str[2]) == 'e') {
				return 1;
			}
			break;
		case 5:
			if (tolower(str[0]) == 'f' && tolower(str[1]) == 'a' && tolower(str[2]) == 'l' && tolower(str[2]) == 's' && tolower(str[2]) == 'e') {
				return 0;
			}
			break;
	}
	// invalid input
	return -1;
}

/**
 * Usage function. Outputs usage information to stderr and exits with
 * the specified return code.
 * @param progname The name of the program. Used in the "usage" part of
 * the output ("usage: <progname> ...").
 * @param retcode The code to exit(3) the program with.
 * @param full Whether to output full usage information or not. If this
 * is 0, then only the "usage: <progname> ..." line is outputted.
 * Otherwise, all usage information is outputted.
 */
static void usage(char *progname, int retcode, int full) {
	fprintf(stderr, "usage: %s %s", progname, short_usage);
	if (full) fputs(long_usage, stderr);
	exit(retcode);
}

/**
 * Version function. Outputs the daemon version to stdout, and exits
 * with the exit code EXIT_SUCCESS.
 */
static void version() {
	printf("v%s", version_str);
	exit(EXIT_SUCCESS);
}

/**
 * Initializes an options_t structure with the default values.
 * @param opts The structure to initialize.
 */
static void init_options(options_t *opts) {	
	memset((void*) opts, 0, sizeof(*opts));
	strncpy(opts->syslog_ident, daemon_name, sizeof(opts->syslog_ident));
}

/**
 * Parses a name-value pair file, placing the parsed data into the argument.
 * The files it parses take the following format:
 *   PARAM=VALUE
 * Where:
 *   PARAM is an alphanumeric identification string, which must begin
 *     with either an alphabetic character or an underscore, and may
 *     otherwise consist of [a-zA-Z0-9_-].
 *   VALUE is the value to assign to the parameter. Everything up to the
 *     end of the current line will be considered as the value, or, if
 *     VALUE begins with a ", up to the next unescaped " (i.e.: up to
 *     the next " which isn't preceded by a \).
 *   Everything after a # character will be treadted as a comment and
 *     will be ignored, unless it appears between two " characters, in
 *     which case it's considered part of the value.
 *   Any whitespace not inside two enclosing " characters is ignored.
 * For example:
 *     # Comment
 *     SomeParam = "Some Value"
 *     _Another-Param=Another Value
 * @param data The raw .rc file data
 * @param opts The destination for the parsed data
 * @return 0 on success, > 0 on invalid format, < 0 on system error (e.g.:
 * couldn't allocate memory).
 */
static int parse_config_file(char *data, options_t *opts) {
	int ret = 0;
	size_t id_len, val_len;
	char c, c_prev, *cur = data, *id_start, *id_end, *id_tmp = NULL, *val_start, *val_end, *val_tmp = NULL;
	
	for (;;) {
		// at the start of a new line
		// free any allocated memory
		if (id_tmp) {
			free((void*) id_tmp);
			id_tmp = NULL;
		}
		
		if (val_tmp) {
			free((void*) val_tmp);
			val_tmp = NULL;
		}

		c = *cur++;
		if (c == '\0') { goto end; }
		
		// skip any whitespace
		skip_whitespace();
		
		// must be a valid identifier start, EOF, or a comment
		if (c == '\0') {
			goto end;
		} else if (c == '#') {
			// comment - find the end of the line
			c = *cur++;
			skip_until_eol();
			if (c == '\0') { goto end; }
			else { continue; }
		} else if (!isvalididentifierstart(c)) {
			// invalid character
			fprintf(stderr, "config: invalid identifier start: %c\n", c);
			errno = EINVAL;
			ret = 1;
			goto end;
		}
		
		// cur - 1 points to the start of an identifier
		id_start = cur - 1;
		// find the end of the identifier
		c = *cur++;
		while (isvalididentifier(c)) { c = *cur++; }
		if (c == '\0' || (!isspace(c) && c != '=')) {
			// invalid EOF, or identifier must be followed by whitespace or a '='
			fprintf(stderr, "config: expected whitespace or '=' after identifier, got: %c\n", c);
			errno = EINVAL;
			ret = 1;
			goto end;
		} else {
			id_end = cur - 1;
			id_len = id_end - id_start + 1;
			id_tmp = (char*) malloc(id_len);
			if (id_tmp == NULL) {
				ret = -1;
				goto end;
			}
			strncpy(id_tmp, (const char*) id_start, id_len);
			id_tmp[id_len - 1] = '\0';
			
			// if necessary, identifiers can be validated here.
			
			if (c != '=') {
				// skip any whitespace
				skip_whitespace_not_eol();
				if (c != '=') {
					// expected a "="
					fprintf(stderr, "config: expected a '=' after \"%s\"\n", id_tmp);
					errno = EINVAL;
					ret = 1;
					goto end;
				}
			}
			
			// cur currently points to the character after the '='
			c = *cur++;
			if (isspace(c)) {
				// skip any whitespace, except for EOL
				skip_whitespace_not_eol();
			}
			
			if (c == '\0' || iseol(c)) {
				// either EOF or EOL - no value
				fprintf(stderr, "config: no value provided for %s\n", id_tmp);
				errno = ENODATA;
				ret = 1;
				goto end;
			}
			// start of the value - may be enclosed in " or not.
			else if (c == '"') {
				// " enclosed value - parse as appropriate.
				val_start = cur;
				val_end = NULL; // if val_end == NULL when the loop
								// is exited, then an error occurred.
				for (;;) {
					c_prev = c;
					c = *cur++;
					if (c == '\0') {
						// premature EOF
						fprintf(stderr, "config: expected terminating '\"', got EOF\n");
						errno = EINVAL;
						break;
					} else if (c == '"') {
						if (c_prev != '\\') {
							// end of value
							val_end = cur - 1;
							break;
						}
					}
					// any other character is fine
				}
				
				if (val_end == NULL) {
					// an error occurred
					ret = 1;
					goto end;
				}
			} else {
				// read up to EOL
				val_start = cur - 1;
				val_end = NULL;
				skip_until_eol_or_comment();
				if (c == '\0' || c == '#') {
					cur--; // so that *cur++ returns 0 or # next time the loop is executed.
					val_end = cur - 1;
				} else {
					// EOL
					val_end = cur - 1;
				}
			}
								
			val_len = val_end - val_start + 1;
			val_tmp = (char*) malloc(val_len);
			if (val_tmp == NULL) {
				ret = -1;
				goto end;
			}
			strncpy(val_tmp, (const char*) val_start, val_len);
			val_tmp[val_len - 1] = '\0';
			
			// the identifier/value pair is now accessible in
			// id_tmp and val_tmp. process them accordingly.
			
			if (identifier_matched("daemonize")) {
				// validate the value
				try_validate_boolean(opts->background);
			} else if (identifier_matched("verbose")) {
				try_validate_boolean(opts->verbose);
			} else if (identifier_matched("syslog_ident")) {
				// store the value
				strncpy(opts->syslog_ident, (const char*) val_tmp, sizeof(opts->syslog_ident));
			} else {
				// invalid identifier - error out
				fprintf(stderr, "config: invalid identifier: %s\n", id_tmp);
				errno = ENOTSUP;
				ret = 1;
				goto end;
			}
		}
	}
	
end:
	if (id_tmp) {
		free((void*) id_tmp);
	}
	if (val_tmp) {
		free((void*) val_tmp);
	}
	return ret;
}

/**
 * Parses command-line arguments, placing the result into the provided
 * destination.
 * @param argc The number of arguments.
 * @param argv An array of the arguments.
 * @param opts A pointer to an options_t structure to place the processed
 * results in.
 * @return 0 on success, -1 on failure.
 */
static int parse_cmdline_opts(int argc, char * const argv[], options_t *opts) {
	int c, options_index;
	
	while ((c = getopt_long(argc, argv, short_options, long_options, &options_index)) != -1) {
		// break statements are preserved to keep compilers happy,
		// not that any should complain.
		switch (c) {
			case 'h':
				usage(argv[0], 0, 1);
				break;
			case 'v':
				version();
				break;
			case 'V':
				opts->verbose = 1;
				break;
			case 'd':
				opts->background = 1;
				break;
			case 'f':
				opts->background = 0;
				break;
			case 'c':
				// as no arguments are being modified, this should be safe
				opts->config_file = (const char*) optarg;
				break;
			case 'Z':
				strncpy(opts->syslog_ident, (const char*) optarg, sizeof(opts->syslog_ident));
				break;
			// process any other options here:
			// case '<character>':
			//	do_something();
			//	break;
			default:
				// getopt() will have outputted an error message
				return -1;
		}
	}
	
	return 0;
}

//----------------------------------------------------------------------------//
// -*'^'*-,__,-*'^'*-,__,-*'^'*-, Core routines -*'^'*-,__,-*'^'*-,__,-*'^'*- //
//----------------------------------------------------------------------------//

/**
 * Main daemon function.
 * @param opts Command-line options for the daemon core.
 * @return 0 on success, anything else on failure
 */
static int daemon_main(options_t *opts) {
	// main daemon functionality goes here
	return 0;
}

int main(int argc, char * const argv[]) {
	int ret;
	pid_t pid, sid;
	options_t opts;

	if (daemon_name == NULL) {
		daemon_name = argv[0];
	}
	
	init_options(&opts);

	// process command-line arguments
	if (parse_cmdline_opts(argc, argv, &opts) < 0) {
		exit(EXIT_FAILURE);
	}
	
	// check if a default config file, should be read, if none were specified
	// on the command-line, doing so only if it actually exists too.
#if (DEFAULT_CONFIG_FILE_PATH != 0)
	if (opts.config_file == NULL) {
		// only try and load the default config file if it exists
		if (file_exists(DEFAULT_CONFIG_FILE_PATH)) {
			opts.config_file = DEFAULT_CONFIG_FILE_PATH;
		}
	}
#endif
	
	// parse the config file, if any
	if (opts.config_file) {
		char *cfg_data = read_entire_file(opts.config_file);
		if (cfg_data == NULL) {
			vperror("could not read config file \"%s\"", opts.config_file);
			exit(EXIT_FAILURE);
		}
		ret = parse_config_file(cfg_data, &opts);
		free((void*) cfg_data);
		if (ret) {
			if (ret < 0) {
				vperror("could not parse config file \"%s\"", opts.config_file);
				exit(EXIT_FAILURE);
			} else {
				// invalid config file
				exit(EXIT_FAILURE);
			}
		}
	}

	// check whether to daemonize or not
	if (opts.background == 1) {
		pid = fork();
		if (pid < 0) {
			// log any errors here
			perror("fork");
			exit(EXIT_FAILURE);
		} else if (pid > 0) {
			// exit the parent process
			printf("Forked, background PID: %u\n", (unsigned) pid);
			exit(EXIT_SUCCESS);
		}
	}

	// change the file mode mask
	umask(0);

	// open the syslog connection immediately, and also log the daemon's PID
	openlog((const char*) opts.syslog_ident, LOG_NDELAY | LOG_PID, LOG_DAEMON);

	// create a new session
	sid = setsid();
	if (sid < 0) {
		perror_syslog("setsid");
		closelog(); // closelog is optional, but may as well be clean
		exit(EXIT_FAILURE);
	}
	
	if (opts.verbose) {
		syslog(LOG_INFO, "Got session ID: %u", (unsigned) sid);
	}

	// change the working directory
	if (chdir(DEFAULT_WORKING_DIR) < 0) {	
		perror_syslog("chdir");
		closelog();
		exit(EXIT_FAILURE);
	}
	
	if (opts.verbose) {
		syslog(LOG_INFO, "Working directory is now %s", DEFAULT_WORKING_DIR);
	}

	// close the standard file descriptors
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	// run the daemon here
	ret = daemon_main(&opts);
	
	if (opts.verbose) {
		syslog(LOG_INFO, "Exiting %s process with return code %d",
			opts.background ? "background" : "foreground", ret);
	}

	// cleanup
	closelog();
	return ret;
}

