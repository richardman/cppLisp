# cppLisp Copyright 2019 Richard Man richard@imagecraft.com
A Lisp Interpreter Written in Modern C++ (C++11, C++14, Boost)

Released under the MIT LICENSE

/* cppLisp implements a usable subset of the Lisp programming language. The list of supported built-in functions are in the
 * routine "add_globals", plus "quote" and "lambda".
 *
 * NO GARBAGE COLLECTION is currently implemented. GC using shared_ptr reference counting is expected to be added in a future
 * release.
 *
 * NOTE: "define" creates/updates a variable in the current scope. "setq" only does update.
 */
