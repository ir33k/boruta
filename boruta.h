/* Boruta v1.0

Currently there is no concept of "instance" when working with Boruta
lib.  There is single global state and memory management is only about
allocating more for new data.  You have been warned.


LANGUAGE:

Boruta uses a concatinative (stack based) language where each value is
a string.  If string matches one of predefined words then that word
logic is executed.  Otherwise string is putted on stack.  Strings are
separated by spaces.  Wrap text in single or double quotes to include
space in single string.


WORDS:

TABLE Defines table name taking one element from stack.  Existing
table is used by INFO, EQ, NEQ, SELECT, INSERT, SET, DEL and DROP.
Non existing table name is used by CREATE.

INFO Prints column names for defined table.  For undefined table
prints list of all tables with number of columns and rows in each.

LOAD Load file using one element from stack as file path.  Loaded file
is parsed adding tables internal database memory.

WRITE Takes one element from stack as file path.  Write database to
that file or to standard output if path is undefined.

EQ Defines "equal" filter conditions for "value column" pairs on stack
for defined table.  Used by SELECT, SET and DEL.

NEQ Same as EQ but it is "not equal" filter.

SKIP Defines how many rows should be skipped on SELECT by taking one
number from stack.

LIMIT Defines how many rows can be printed on SELECT by taking one
number from stack.

SELECT Selects rows from defined table with specified column names
taken from stack.  For "*" column name all table columns are taken.

CREATE Adds new table with name defined by TABLE and column names
taken from stack.

INSERT Adds new row to defined table with "value column" pairs taken
from stack.

SET Modify "value column" pairs for defined table for every row that
passes EQ and NEQ filters.

DEL Delete rows from defined table for matchin EQ and NEQ filters.

DROP Deletes defined table or all tables if stack is empty.

NULL Puts empty ("---") value on stack.

NOW Puts current date in "%Y-%M-%D" format on stack.


API:

Callback boruta_cb_t is called each time boruta() outputs row data
when running INFO or SELECT words or error occured.  CTX points at
context defined in boruta().  On error WHY will be a string with
message, other args should be ignored.  Else CN will define number
of columns and rows in COLS and ROWS string arrays.

To run database query call boruta() with optional CB callback and
optional CTX context of user data.  FMT is a format string like in
printf() being a valid query.

*/

typedef void (*boruta_cb_t)(void *ctx, char *why,
                            int cn, char **cols, char **row);

void boruta(boruta_cb_t cb, void *ctx, char *fmt, ...);
