# JsonCalc

JsonCalc is program for exploring or converting JSON data.
Its main features are:

* It uses a syntax that's a combination of JavaScript and SQL.
  If you're familiar with those two languages then JsonCalc should be
  fairly intuitive.
  There are a few added operators and functions for things like filtering
  arrays and joining tables, but they're easy to learn.

  - Javascript features include nearly all operators, and the most common
    functions,  Many statements are available too.  You can define consts,
    vars, and functions.

  - SQL features include a fairly complete SELECT clause, EXPLAIN,
    aggregate functions like COUNT() and SUM(), operators including
    BETWEEN...AND, LIKE, and IS NULL.

  - Added features include an _array_*@*_expr_ operator for filtering arrays
    and tables, intuitive support for ISO 8601 times/dates/periods, and some
    carefully chosen extensions to JavaScript's functions.

* It can be used interactively, or in a shell script, or as its own scripting
  tool.

  - When run interactively, it supports line editing, history, and name
  completion.

  - When used in a shell script, you can pass values in to JsonCalc via
    _name_=_value_ arguments on the command line, or via $_name_=_value_
    environment variables.
    Collecting JSON output is easy.
    If you want your shell to process rows of a table, that's pretty easy too
    because JsonCalc supports a "shell" output format, where each row is output
    as a line of _name_=_value_ pairs with shell quoting.

  - When used as a self-contained scripting language, it can easily generate
    or convert JSON data.
    With plugins, it can be used to fetch data via the web, and write it to
    a database.

* It allows simple arithmetic on ISO-8601 dates, times, datetimes, and periods.
  There are also "Swiss army knife" functions for doing things like converting
  to other formats, and adjusting the timezone.

* Member names can be case-insensitive.
  This can save you a bit of frustration for some data sets.
  It feels almost as important as name completion.

* Most of the logic is implemented in a library.
  Implementing it this way allows you to embed JsonCalc in other programs.
  The library has "hooks" that let you add your own extensions to the syntax.
  You can also use its low-level functions to manipulate JSON data in your
  own C/C++ programs.

* It supports plugins.
  Plugins are available for sending web requests, handling XML data, directly
  interfacing with MySQL or SQLLite databases, logging, cacheing, and more.
  You can implement your own plugins that use the above-mentioned "hooks"
  to extend the syntax.

* The JSON data parser is quick.

* It fully supports UTF-8 text.

* Tables (arrays of objects) can be output in a nice grid format.
  CSV and Shell output formats are also supported.

* It is well documented.

  - Online documentation is available at
    [https://www.jsoncalc.net](https://www.jsoncalc.net).
    This site is mostly intended to serve as a quick reference, but there
    are also some tutorials there.

  - It has a fairly extensive "help" command.
