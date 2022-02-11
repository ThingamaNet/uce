# uce

## Current State

This is in the early stages of development. Don't use this for anything important (or at all)!

## Udo's C++ Entry Points

The aim of this project is to make a PHP-like runtime that enables server-side "scripting" using C/C++. At the core is a multi-worker FastCGI server that can be talked to from Nginx or similar front-end servers. UCE has a shared-nothing isolated architecture to serve page requests. To minimize the potential for memory leaks, UCE uses a per-request memory arena. UCE also provides a PHP-like API to ease web development. Advanced features such as an integrated WebSockets broker are planned. UCE aims to use minimal dependencies (at the moment, the only dependency is the Clang compiler). UCE pages are automatically recompiled and dynamically reloaded as necessary.

# API

Memcache Functions
==================

memcache\_command
-----------------

String memcache\_command(u64 connection, String command)

### Parameters

**connection** : connection handle

**command** : string containing the Memcache command

**return value** : string containing the Memcache server's response

### Description

Executes a command on an open memcache connection.

memcache\_connect
-----------------

u64 memcache\_connect(String host = "127.0.0.1", short port = 11211)

### Parameters

**host** : optional host name of the memcache server, defaults to local address 127.0.0.1

**port** : optional memcache server's port, defaults to 11211

**return value** : the connection handle (or -1 if an error occurred)

### Description

Connects to a memcache server instance.

memcache\_delete
----------------

bool memcache\_delete(u64 connection, String key)

### Parameters

**connection** : connection handle

**key** : key string

**return value** : true if the operation was successful

### Description

Deletes entry specified by the 'key'.

memcache\_get
-------------

String memcache\_get(u64 connection, String key, String default\_value = "")

### Parameters

**connection** : connection handle

**key** : key string

**default\_value** : optional default value

**return value** : value that was returned by the Memcache server

### Description

Retrieves a value from an existing connection to a Memcache server.

memcache\_get\_multiple
-----------------------

StringMap memcache\_get\_multiple(u64 connection, StringList keys)

### Parameters

**connection** : connection handle

**keys** : a list of strings containing the keys to be retrieved

**return value** : a StringMap with the retrieved entries

### Description

Retrieves a bunch of entries all at once.

memcache\_set
-------------

bool memcache\_set(u64 connection, String key, String value, u64 expires\_in = 60\*60)

### Parameters

**connection** : connection handle

**key** : the entry's key

**value** : the value to be set

**expires\_in** : optional expiration timeout, defaults to one hour

**return value** : true if the operation was successful

### Description

Stores a 'value' on the Memcache server.

MySQL Functions
===============

mysql\_connect
--------------

MySQL\* mysql\_connect(String host = "localhost", String username = "root", String password = "")

### Parameters

**host** : host name of the MySQL server

**username** : user name

**password** : password

**return value** : pointer to the MySQL connection struct

### Description

Establishes a connection to a MySQL server.

mysql\_disconnect
-----------------

void mysql\_disconnect(MySQL\* m)

### Parameters

**m** : pointer to an existing MySQL connection struct

### Description

Closes a connection to a MySQL server.

mysql\_error
------------

String mysql\_error(MySQL\* m)

### Parameters

**m** : pointer to a MySQL connection struct

**return value** : MySQL error message (if present, otherwise empty string)

### Description

Returns the last error message from a connection to a MySQL server.

mysql\_escape
-------------

String mysql\_escape(String raw, char quote\_char)

### Parameters

**raw** : the string to be escaped

**quote\_char** : the character that should be used to wrap the string (pass NULL for no wrapping)

**return value** : the safe version of the 'raw' string

### Description

Escapes a string such that it can be passed as a safe value into an SQL expression.

mysql\_insert\_id
-----------------

u64 mysql\_insert\_id(MySQL\* m)

### Parameters

**m** : pointer to an active MySQL connection

**return value** : the last used automatic row ID

### Description

This retrieves the last row ID that was used for a column with an AUTO\_INCREMENT row key.

mysql\_query
------------

DTree mysql\_query(MySQL\* m, String q, StringMap params)

### Parameters

**m** : pointer to an active MySQL connection struct

**q** : a string containing a MySQL query

**params** : optional, a list of query parameter keys and values

**return value** : a list of rows returned from executing the query

### Description

Executes a MySQL query and returns the resulting data (if any).

### Examples

(tbd)

Noise/Hash Functions
====================

draw\_float
-----------

f64 draw\_float(f64 from, f64 to)

### Parameters

**from** : minimum value

**to** : maximum value

**return value** : a noise value between 'from' and 'to'

### Description

This function works exactly like generate\_float(), but context->random\_index is used for the 'index' value and context->random\_seed is used for the seed. After this function has been called, the context->random\_index is increased by one. At the start of every request, context->random\_seed is automatically populated with a new seed value.

draw\_int
---------

u64 draw\_int(u64 from, u64 to)

### Parameters

**from** : minimum value

**to** : maximum value

**return value** : a noise value between 'from' and 'to'

### Description

This function works exactly like generate\_int(), but context->random\_index is used for the 'index' value and context->random\_seed is used for the seed. After this function has been called, the context->random\_index is increased by one. At the start of every request, context->random\_seed is automatically populated with a new seed value.

gen\_noise01
------------

u32 noise01(u64 index, u64 seed = 0)

### Parameters

**index** : index position

**seed** : seed set (defaults to 0)

**return value** : a noise value from 0 to 1

### Description

Generates a noise value in the range from 0 to 1 for the given 'index' and 'seed' values.

gen\_noise32
------------

u32 noise32(u32 index, u32 seed = 0)

### Parameters

**index** : index position

**seed** : seed set (defaults to 0)

**return value** : a noise value given the 'index' and 'seed' values.

### Description

Generates a noise value for the given 'index' and 'seed' values.

gen\_noise64
------------

u32 noise64(u64 index, u64 seed = 0)

### Parameters

**index** : index position

**seed** : seed set (defaults to 0)

**return value** : a noise value given the 'index' and 'seed' values.

### Description

Generates a noise value for the given 'index' and 'seed' values.

gen\_float
----------

f64 generate\_float(f64 from, f64 to, u64 index, u64 seed = 0)

### Parameters

**from** : minimum result

**to** : maximum result

**index** : index position to generate number from

**seed** : seed position to generate number from (defaults to 0)

**return value** : noise value

### Description

Generates a noise value between 'from' and 'to', given the 'index' and 'seed' numbers.

gen\_int
--------

u64 generate\_int(u64 from, u64 to, u64 index, u64 seed = 0)

### Parameters

**from** : minimum result

**to** : maximum result

**index** : index position to generate number from

**seed** : seed position to generate number from (defaults to 0)

**return value** : noise value

### Description

Generates a noise value between 'from' and 'to', given the 'index' and 'seed' numbers.

gen\_sha1
---------

String sha1(String s, bool as\_binary = false)

### Parameters

**s** : data to be hashed

**as\_binary** : when set to false, returns hash in hexadecimal notation (defaults to false)

**return value** : the resulting hash value

### Description

Returns the sha1 hash of 's'.

Output Buffer Functions
=======================

ob\_clear
---------

void ob\_clear()

### Parameters

**(none)** :

### Description

Discard the current output buffer.

ob\_get
-------

String ob\_get()

### Parameters

**return value** : content of the current output buffer

### Description

Returns the contents of the current output buffer.

ob\_get\_clear
--------------

String ob\_get\_clear()

### Parameters

**return value** : content of the current output buffer

### Description

Returns the contents of the current output buffer and then discards the buffer.

ob\_start
---------

void ob\_start()

### Parameters

**(none)** :

### Description

Starts a new output buffer. All subsequent output will be directed into this buffer.

Sessions
========

make\_session\_id
-----------------

String make\_session\_id()

### Parameters

**return value** : a new session ID

### Description

Creates a session ID

session\_destroy
----------------

void session\_destroy(String session\_name)

### Parameters

**session\_name** : the name of the session

### Description

Deletes the cookie specified by 'session\_name' and clears the data stored under the session ID. This empties the 'context->session\_id' and 'context->session' variables.

session\_start
--------------

String session\_start(String session\_name)

### Parameters

**return value** : the session ID, defaults to "uce-session"

### Description

Starts session or connects to existing session. This function sets a cookie with the name contained in 'session\_name' if it does not exist and fills that cookie with a new unique session ID. It then loads the session data for that session ID. Afterwards, the following fields are populated in the 'context' variable:

context->session\_id : the current session ID

context->session\_name : the current session cookie name

context->session : the current session data. The session data is automatically saved after a request completes.

Socket Functions
================

socket\_close
-------------

void socket\_close(u64 sockfd)

### Parameters

**sockfd** : socket handle

### Description

Closes an existing socket connection.

socket\_connect
---------------

u64 socket\_connect(String host, short port)

### Parameters

**host** : host name

**port** : port number

**return value** : the socket handle

### Description

Opens a socket connection to the given 'host' and 'port'.

socket\_read
------------

String socket\_read(u64 sockfd, u32 max\_length = 1024\*128, u32 timeout = 1);

### Parameters

**sockfd** : socket handle

**max\_length** : optional maximum data size, defaults to 128kBytes

**timeout** : optional operation timeout, defaults to one second

**return value** : string containing the data that was read

### Description

Reads data from a socket connection.

socket\_write
-------------

bool socket\_write(u64 sockfd, String data)

### Parameters

**sockfd** : socket handle

**data** : a string containing the data to be written to the socket

**return value** : true if the write operation was successful

### Description

Writes a string of 'data' to the given socket.

String Functions
================

filter
------

StringList filter(StringList items, function f)

vector filter(vector items, function f)

### Parameters

**items** : list of items to be filtered

**f** : a function that decides which items should be in the new list

**return value** : a new list

### Description

Returns a list containing the members of 'items' for which 'f' returned boolean true.

first
-----

String first(String... args)

### Parameters

**args** : a variable number of String arguments

**return value** : first of the 'args' that was not empty.

### Description

Given a variable number of String parameters, the first() function returns the first of these parameters that was not empty. Leading and trailing whitespace characters are not considered, resulting in a string that contains only whitespace characters being considered empty.

join
----

String join(StringList l, String delim = "\\n")

### Parameters

**l** : list of strings to be joined

**delim** : delimiter (defaults to newline character)

**return value** : a string containing items joined by 'delim'

### Description

Joins the items contained in 'l' into a single String.

nibble
------

String nibble(String& haystack, String delim)

### Parameters

**haystack** : string to be nibbled at

**delim** : delimiter

**return value** : string before first occurrence of 'delim'

### Description

Returns the part of 'haystack' before the first occurrence of 'delim', removing the corresponding part from 'haystack' (including 'delim'). If the substring 'delim' does not occurr in 'haystack', the entire string is returned and 'haystack' is set to an empty string.

split
-----

StringList split(String str, String delim)

### Parameters

**str** : string to be split

**delim** : delimiter

**return value** : a list of strings

### Description

Splits 'str' into multiple strings based on the given delimiter 'delim'.

split\_space
------------

StringList split\_space(String str)

### Parameters

**str** : string to be split

**return value** : a list of strings

### Description

Splits 'str' into multiple strings along any whitespace characters (multiple whitespace characters count as one).

split\_utf8
-----------

StringList split\_utf8(String str, bool compound\_characters = false)

### Parameters

**str** : string to be split

**compound\_characters** : optional, if true tries to combine compound characters

**return value** : a list of Unicode characters

### Description

Splits the string 'str' into its constituent Unicode code points.

If 'compound\_characters' is true, split\_utf8 will attempt to combine compound characters based on very simple rules:

*   combine characters if they're connected by a Zero-Width Joiner (ZWJ) character

*   combine two characters if they're both a Regional Indicator Symbol Letter

*   if a character is a Variation Selector, append it to the previous character

*   in all other cases, characters remain on their own

replace
-------

String replace(String s, String search, String replace\_with)

### Parameters

**s** : the string where replacements should happen

**search** : the string that should be searched for

**replace\_with** : the string that should appear in places where 'search' occurs

**return value** : a version of 's' where all instances of 'search' have been replaced with 'replace\_with'

### Description

Replace all occurrences of 'search' with the string defined in 'replace\_with'.

to\_lower
---------

String to\_lower(String s)

### Parameters

**s** : the string to be converted

**return value** : returns a version of 's' where all upper case characters have been changed into lower case

### Description

Returns a lower case version of the input string 's'.

Note: this function is not yet Unicode-aware.

to\_upper
---------

String to\_upper(String s)

### Parameters

**s** : the string to be converted

**return value** : returns a version of 's' where all lower case characters have been changed into upper case

### Description

Returns a upper case version of the input string 's'.

Note: this function is not yet Unicode-aware.

trim
----

String trim(String raw)

### Parameters

**raw** : string to be trimmed

**return value** : string with leading and trailing whitespace characters removed

### Description

Returns a string where leading an trailing whitespace characters have been trimmed off.

File System Functions
=====================

basename
--------

String basename(String fn)

### Parameters

**fn** : raw filename

**return value** : the file's name

### Description

Isolates the file name component from a path/file name.

dirname
-------

String dirname(String fn)

### Parameters

**fn** : raw filename

**return value** : the directory's name

### Description

Isolates the directory name component from a path/file name.

expand\_path
------------

String expand\_path(String path, String relative\_to\_path = "")

### Parameters

**path** : a relative path

**relative\_to\_path** : optional, expand relative to this path (if not given, the current path is used)

**return value** : expanded version of the 'path'

### Description

Converts a relative path name into an absolute path, using the current working directory as a base.

file\_append
------------

void file\_append(String file\_name, ...val)

### Parameters

**file\_name** : file name of file that should be written to

**...val** : one or more values that should be written into the file

### Description

Opens or creates a given file and appends data to it.

file\_exists
------------

bool file\_exists(String path)

### Parameters

**path** : the path name to be checked

**return value** : true if the file exists

### Description

Checks whether the file or path specified by 'path' exists.

file\_get\_contents
-------------------

String file\_get\_contents(String file\_name)

### Parameters

**file\_name** : file name of file that should be read

**return value** : String containing the file's contents

### Description

Reads the file identified by 'file\_name' and returns it as a String. If the file cannot be read, this function will return an empty string.

file\_mtime
-----------

time\_t file\_mtime(String file\_name)

### Parameters

**file\_name** : name of the file

**return value** : Unix time stamp of the file's last modification

### Description

Retrieves the last modification date of 'file\_name' as a Unix timestamp.

file\_put\_contents
-------------------

bool file\_put\_contents(String file\_name, String content)

### Parameters

**file\_name** : file name of file that should be written

**content** : content that should be written

**return value** : true if write was successful

### Description

Writes the String 'content' into a file identified by 'file\_name'. Any pre-existing content of the file will be overwritten.

get\_cwd
--------

String get\_cwd()

### Parameters

**return value** : the current working directory

### Description

Returns the current working directory.

ls
--

StringList ls(String path)

### Parameters

**path** : a filesystem path

**return value** : list of directory entries

### Description

Returns a list of files and subdirectories within the given 'path'.

mkdir
-----

bool mkdir(String path)

### Parameters

**path** : the path name to be created

**return value** : returns true if the directory was successfully created

### Description

Creates a directory stated by 'path'

set\_cwd
--------

void set\_cwd(String path)

### Parameters

**path** : the new working directory

### Description

Sets a new working directory.

shell\_escape
-------------

String shell\_escape(String raw)

### Parameters

**raw** : string that should be escaped

**return value** : escaped version of 'raw'

### Description

Escapes a parameter for shell\_exec

shell\_exec
-----------

String shell\_exec(String cmd)

### Parameters

**cmd** : string that contains the shell command line to be executed

**return value** : output of the command execution

### Description

Executes a Linux shell command and returns the generated output

unlink
------

void unlink(String file\_name)

### Parameters

**file\_name** : name of the file

### Description

Deletes the file identified by 'file\_name'.

Task API
========

kill
----

s64 kill(pid\_t pid, s64 sig)

### Parameters

**pid** : PID of the process

**sig** : signal number

**return value** : 0 if signal was sent, -1 otherwise

### Description

This is the standard POSIX kill() function, provided here for reference.

Possible signal numbers are: SIGABND, SIGABRT, SIGALRM, SIGBUS, SIGFPE, SIGHUP, SIGILL, SIGINT, SIGKILL, SIGPIPE, SIGPOLL, SIGPROF, SIGQUIT, SIGSEGV, SIGSYS, SIGTERM, SIGTRAP, SIGURG, SIGUSR1, SIGUSR2, SIGVTALRM, SIGXCPU, SIGXFSZ, SIGCHLD, SIGIO, SIGIOERR, SIGWINCH, SIGSTOP, SIGTSTP, SIGTTIN, SIGTTOU, SIGCONT.

task
----

pid\_t task(String key, std::function exec\_func)

### Parameters

**key** : string uniquely identifying the task

**exec\_func** : function to execute

**return value** : the process ID of the started (or still running) task

### Description

task() starts the 'exec\_func' in a new process and returns that process' ID. If a process with the same 'key' is already running, task will not start a new process but instead just return the PID of the process that is already running.

task\_pid
---------

pid\_t task\_pid(String key)

### Parameters

**key** : string uniquely identifying the task

**return value** : the process ID of the task

### Description

Checks whether a process with the given 'key' is running and returns its PID if it is. Returns 0 otherwise.

Time and Date Functions
=======================

microtime
---------

f64 microtime()

### Parameters

**return value** : current Unix timestamp

### Description

Returns a 64 bit float containing the current Unix timestamp with millisecond accuracy or better.

time
----

u64 time()

### Parameters

**return value** : second-accurate current Unix timestamp

### Description

Returns a 64 bit integer containing the current Unix timestamp.

date
----

String date(String format = "", u64 timestamp = 0)

### Parameters

**format** : formatting string specifying the date format

**timestamp** : optional timestamp value, defaults to current time

**return value** : a formatted date

### Description

Returns a formatted date. This is based on the Linux date() command. The formatting string supports the following sequences:

       %%     a literal %

       %a     locale's abbreviated weekday name (e.g., Sun)

       %A     locale's full weekday name (e.g., Sunday)

       %b     locale's abbreviated month name (e.g., Jan)

       %B     locale's full month name (e.g., January)

       %c     locale's date and time (e.g., Thu Mar  3 23:05:25 2005)

       %C     century; like %Y, except omit last two digits (e.g., 20)

       %d     day of month (e.g., 01)

       %D     date; same as %m/%d/%y

       %e     day of month, space padded; same as %\_d

       %F     full date; like %+4Y-%m-%d

       %g     last two digits of year of ISO week number (see %G)

       %G     year of ISO week number (see %V); normally useful only

              with %V

       %h     same as %b

       %H     hour (00..23)

       %I     hour (01..12)

       %j     day of year (001..366)

       %k     hour, space padded ( 0..23); same as %\_H

       %l     hour, space padded ( 1..12); same as %\_I

       %m     month (01..12)

       %M     minute (00..59)

       %n     a newline

       %N     nanoseconds (000000000..999999999)

       %p     locale's equivalent of either AM or PM; blank if not known

       %P     like %p, but lower case

       %q     quarter of year (1..4)

       %r     locale's 12-hour clock time (e.g., 11:11:04 PM)

       %R     24-hour hour and minute; same as %H:%M

       %s     seconds since 1970-01-01 00:00:00 UTC

       %S     second (00..60)

       %t     a tab

       %T     time; same as %H:%M:%S

       %u     day of week (1..7); 1 is Monday

       %U     week number of year, with Sunday as first day of week

              (00..53)

       %V     ISO week number, with Monday as first day of week (01..53)

       %w     day of week (0..6); 0 is Sunday

       %W     week number of year, with Monday as first day of week

              (00..53)

       %x     locale's date representation (e.g., 12/31/99)

       %X     locale's time representation (e.g., 23:13:48)

       %y     last two digits of year (00..99)

       %Y     year

       %z     +hhmm numeric time zone (e.g., -0400)

       %:z    +hh:mm numeric time zone (e.g., -04:00)

       %::z   +hh:mm:ss numeric time zone (e.g., -04:00:00)

       %:::z  numeric time zone with : to necessary precision (e.g.,

              -04, +05:30)

       %Z     alphabetic time zone abbreviation (e.g., EDT)

       By default, date pads numeric fields with zeroes.  The following

       optional flags may follow '%':

       -      (hyphen) do not pad the field

       \_      (underscore) pad with spaces

       0      (zero) pad with zeros

       +      pad with zeros, and put '+' before future years with >4

              digits

       ^      use upper case if possible

       #      use opposite case if possible

gmdate
------

String gmdate(String format = "", u64 timestamp = 0)

### Parameters

**format** : formatting string specifying the date format

**timestamp** : optional timestamp value, defaults to current time

**return value** : a formatted date

### Description

Returns a formatted date in the GMT/UTC timezone. This is based on the Linux date() command. The formatting string supports the following sequences:

       %%     a literal %

       %a     locale's abbreviated weekday name (e.g., Sun)

       %A     locale's full weekday name (e.g., Sunday)

       %b     locale's abbreviated month name (e.g., Jan)

       %B     locale's full month name (e.g., January)

       %c     locale's date and time (e.g., Thu Mar  3 23:05:25 2005)

       %C     century; like %Y, except omit last two digits (e.g., 20)

       %d     day of month (e.g., 01)

       %D     date; same as %m/%d/%y

       %e     day of month, space padded; same as %\_d

       %F     full date; like %+4Y-%m-%d

       %g     last two digits of year of ISO week number (see %G)

       %G     year of ISO week number (see %V); normally useful only

              with %V

       %h     same as %b

       %H     hour (00..23)

       %I     hour (01..12)

       %j     day of year (001..366)

       %k     hour, space padded ( 0..23); same as %\_H

       %l     hour, space padded ( 1..12); same as %\_I

       %m     month (01..12)

       %M     minute (00..59)

       %n     a newline

       %N     nanoseconds (000000000..999999999)

       %p     locale's equivalent of either AM or PM; blank if not known

       %P     like %p, but lower case

       %q     quarter of year (1..4)

       %r     locale's 12-hour clock time (e.g., 11:11:04 PM)

       %R     24-hour hour and minute; same as %H:%M

       %s     seconds since 1970-01-01 00:00:00 UTC

       %S     second (00..60)

       %t     a tab

       %T     time; same as %H:%M:%S

       %u     day of week (1..7); 1 is Monday

       %U     week number of year, with Sunday as first day of week

              (00..53)

       %V     ISO week number, with Monday as first day of week (01..53)

       %w     day of week (0..6); 0 is Sunday

       %W     week number of year, with Monday as first day of week

              (00..53)

       %x     locale's date representation (e.g., 12/31/99)

       %X     locale's time representation (e.g., 23:13:48)

       %y     last two digits of year (00..99)

       %Y     year

       %z     +hhmm numeric time zone (e.g., -0400)

       %:z    +hh:mm numeric time zone (e.g., -04:00)

       %::z   +hh:mm:ss numeric time zone (e.g., -04:00:00)

       %:::z  numeric time zone with : to necessary precision (e.g.,

              -04, +05:30)

       %Z     alphabetic time zone abbreviation (e.g., EDT)

       By default, date pads numeric fields with zeroes.  The following

       optional flags may follow '%':

       -      (hyphen) do not pad the field

       \_      (underscore) pad with spaces

       0      (zero) pad with zeros

       +      pad with zeros, and put '+' before future years with >4

              digits

       ^      use upper case if possible

       #      use opposite case if possible

parse\_time
-----------

u64 parse\_time(String time\_string)

### Parameters

**time\_string** : a string containing a date and/or time in text form

**return value** : the interpreted 'time\_string' as a Unix timestamp

### Description

Attempts to parse the given 'time\_string' into a Unix timestamp.

URI Functions
=============

encode\_query
-------------

String encode\_query(StringMap map)

### Parameters

**q** : StringMap containing URL parameters to be encoded

**return value** : a string with the encoded parameters

### Description

Encodes a StringMap containing URL parameters into a single String.

make\_session\_id
-----------------

String make\_session\_id()

### Parameters

**return value** : a new session ID

### Description

Creates a session ID

parse\_query
------------

StringMap parse\_query(String q)

### Parameters

**q** : string containing URL parameters

**return value** : a StringMap containing the parameters

### Description

Decodes a string of the format 'a=b&c=d' into a StringMap containing keyed entries.

uri\_decode
-----------

String uri\_decode(String s)

### Parameters

**s** : string containing URI encoded data

**return value** : a string that contains the decoded version of 's'

### Description

Decodes an URI-encoded string 's'.

uri\_encode
-----------

String uri\_encode(String s)

### Parameters

**s** : string that should be encoded

**return value** : an URI-encoded version of 's'

### Description

URI-encodes a string.

Other Functions and Data Structures
===================================

context
-------

Request\* context;

### ServerState\* server

Contains the current server state

### StringMap params

All FastCGI server parameters

### StringMap get

The current request's GET variables

### StringMap post

The current request's POST variables

### StringMap cookies

Cookies that have been transmitted by the browser

### StringMap session

The current session

### String session\_id

ID of the session cookie

String session\_name

Name of the session cookie

### DTree var

Variable user-defined data

### std::vector<UploadedFile> uploaded\_files

Files that have been uploaded in the current request

### StringMap header

Headers to be sent back to the browser

### StringList set\_cookies;

Cookies that should be sent back to the browser

### u64 random\_seed

The current request's "random" noise generator seed

### u64 random\_index

The current request's "random" noise generator index position

### MemoryArena\* mem

Contains the current request's memory arena

### bool flags.log\_request

Whether the request should be logged

### Stats

u32 stats.bytes\_written

f64 stats.time\_init

f64 stats.time\_start

f64 stats.time\_end

### invoke(String file\_name, \[DTree& call\_param\])

Invokes the UCE file 'file\_name'

#load
-----

#load "myfile.uce"

### Parameters

**file name** : name of an UCE file that should be included

### Description

Includes another UCE file

float\_val
----------

f64 float\_val(String s)

### Parameters

**s** : string to be converted

**return value** : a f64 containing the number (0 if no number could be identified).

### Description

Extracts a floating point number from a String.

html\_escape
------------

String html\_escape(String s)

### Parameters

**s** : string to be escaped

**return value** : an HTML-safe escaped version of 's'

### Description

Returns a version of the input string where the following characters have been replace by HTML entities:

*   & → &amp
*   < → lt;
*   \> → &gt;
*   " → &quot;

int\_val
--------

u64 int\_val(String s, u32 base = 10)

### Parameters

**s** : string to be converted

**base** : number system base (default 10)

**return value** : a u64 containing the number (0 if no number could be identified).

### Description

Extracts an integer from a String.

json\_decode
------------

DTree json\_decode(String s)

### Parameters

**s** : string containing JSON data

**return value** : a DTree object containing the deserialized JSON data

### Description

Deserializes 's' into a DTree structure.

json\_encode
------------

String json\_encode(DTree t)

### Parameters

**t** : DTree object to be serialized

**return value** : string containing the JSON result

### Description

Serializes a DTree structure 't' into a String in JSON notation.

print
-----

void print(...val)

### Parameters

**...val** : one or more values that should be output

### Description

Appends data to the current request's output stream.

var\_dump
---------

String var\_dump(StringMap t, String prefix = "", String postfix = "\\n")

String var\_dump(StringList t, String prefix = "", String postfix = "\\n")

String var\_dump(DTree t, String prefix = "", String postfix = "\\n")

### Parameters

**t** : object to be dumped into a string

**return value** : string containing a human-friendly representation of 't'

### Description

Returns a string representation of 't' intended for debugging.
