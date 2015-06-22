/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include "http_parser.h"
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h> /* rand */
#include <string.h>
#include <stdarg.h>

#if defined(__APPLE__)
# undef strlcat
# undef strlncpy
# undef strlcpy
#endif  /* defined(__APPLE__) */

#undef TRUE
#define TRUE 1
#undef FALSE
#define FALSE 0

#define MAX_HEADERS 13
#define MAX_ELEMENT_SIZE 2048
#define MAX_CHUNKS 16

#define MIN(a,b) ((a) < (b) ? (a) : (b))

static http_parser *parser;

struct message {
  const char *name; // for debugging purposes
  const char *raw;
  enum http_parser_type type;
  enum http_method method;
  int status_code;
  char response_status[MAX_ELEMENT_SIZE];
  char request_path[MAX_ELEMENT_SIZE];
  char request_url[MAX_ELEMENT_SIZE];
  char fragment[MAX_ELEMENT_SIZE];
  char query_string[MAX_ELEMENT_SIZE];
  char body[MAX_ELEMENT_SIZE];
  size_t body_size;
  const char *host;
  const char *userinfo;
  uint16_t port;
  int num_headers;
  enum { NONE=0, FIELD, VALUE } last_header_element;
  char headers [MAX_HEADERS][2][MAX_ELEMENT_SIZE];
  int should_keep_alive;

  int num_chunks;
  int num_chunks_complete;
  int chunk_lengths[MAX_CHUNKS];

  const char *upgrade; // upgraded body

  unsigned short http_major;
  unsigned short http_minor;

  int message_begin_cb_called;
  int headers_complete_cb_called;
  int message_complete_cb_called;
  int message_complete_on_eof;
  int body_is_final;
};

static int currently_parsing_eof;

static struct message messages[5];
static int num_messages;
static http_parser_settings *current_pause_parser;

/* * R E Q U E S T S * */
const struct message requests[] =
#define CURL_GET 0
{ {.name= "curl get"
  ,.type= HTTP_REQUEST
  ,.raw= "GET /test HTTP/1.1\r\n"
         "User-Agent: curl/7.18.0 (i486-pc-linux-gnu) libcurl/7.18.0 OpenSSL/0.9.8g zlib/1.2.3.3 libidn/1.1\r\n"
         "Host: 0.0.0.0=5000\r\n"
         "Accept: */*\r\n"
         "\r\n"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_GET
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/test"
  ,.request_url= "/test"
  ,.num_headers= 3
  ,.headers=
    { { "User-Agent", "curl/7.18.0 (i486-pc-linux-gnu) libcurl/7.18.0 OpenSSL/0.9.8g zlib/1.2.3.3 libidn/1.1" }
    , { "Host", "0.0.0.0=5000" }
    , { "Accept", "*/*" }
    }
  ,.body= ""
  }

#define FIREFOX_GET 1
, {.name= "firefox get"
  ,.type= HTTP_REQUEST
  ,.raw= "GET /favicon.ico HTTP/1.1\r\n"
         "Host: 0.0.0.0=5000\r\n"
         "User-Agent: Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9) Gecko/2008061015 Firefox/3.0\r\n"
         "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
         "Accept-Language: en-us,en;q=0.5\r\n"
         "Accept-Encoding: gzip,deflate\r\n"
         "Accept-Charset: ISO-8859-1,utf-8;q=0.7,*;q=0.7\r\n"
         "Keep-Alive: 300\r\n"
         "Connection: keep-alive\r\n"
         "\r\n"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_GET
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/favicon.ico"
  ,.request_url= "/favicon.ico"
  ,.num_headers= 8
  ,.headers=
    { { "Host", "0.0.0.0=5000" }
    , { "User-Agent", "Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9) Gecko/2008061015 Firefox/3.0" }
    , { "Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8" }
    , { "Accept-Language", "en-us,en;q=0.5" }
    , { "Accept-Encoding", "gzip,deflate" }
    , { "Accept-Charset", "ISO-8859-1,utf-8;q=0.7,*;q=0.7" }
    , { "Keep-Alive", "300" }
    , { "Connection", "keep-alive" }
    }
  ,.body= ""
  }

#define DUMBFUCK 2
, {.name= "dumbfuck"
  ,.type= HTTP_REQUEST
  ,.raw= "GET /dumbfuck HTTP/1.1\r\n"
         "aaaaaaaaaaaaa:++++++++++\r\n"
         "\r\n"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_GET
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/dumbfuck"
  ,.request_url= "/dumbfuck"
  ,.num_headers= 1
  ,.headers=
    { { "aaaaaaaaaaaaa",  "++++++++++" }
    }
  ,.body= ""
  }

#define FRAGMENT_IN_URI 3
, {.name= "fragment in url"
  ,.type= HTTP_REQUEST
  ,.raw= "GET /forums/1/topics/2375?page=1#posts-17408 HTTP/1.1\r\n"
         "\r\n"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_GET
  ,.query_string= "page=1"
  ,.fragment= "posts-17408"
  ,.request_path= "/forums/1/topics/2375"
  /* XXX request url does include fragment? */
  ,.request_url= "/forums/1/topics/2375?page=1#posts-17408"
  ,.num_headers= 0
  ,.body= ""
  }

#define GET_NO_HEADERS_NO_BODY 4
, {.name= "get no headers no body"
  ,.type= HTTP_REQUEST
  ,.raw= "GET /get_no_headers_no_body/world HTTP/1.1\r\n"
         "\r\n"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE /* would need Connection: close */
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_GET
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/get_no_headers_no_body/world"
  ,.request_url= "/get_no_headers_no_body/world"
  ,.num_headers= 0
  ,.body= ""
  }

#define GET_ONE_HEADER_NO_BODY 5
, {.name= "get one header no body"
  ,.type= HTTP_REQUEST
  ,.raw= "GET /get_one_header_no_body HTTP/1.1\r\n"
         "Accept: */*\r\n"
         "\r\n"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE /* would need Connection: close */
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_GET
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/get_one_header_no_body"
  ,.request_url= "/get_one_header_no_body"
  ,.num_headers= 1
  ,.headers=
    { { "Accept" , "*/*" }
    }
  ,.body= ""
  }

#define GET_FUNKY_CONTENT_LENGTH 6
, {.name= "get funky content length body hello"
  ,.type= HTTP_REQUEST
  ,.raw= "GET /get_funky_content_length_body_hello HTTP/1.0\r\n"
         "conTENT-Length: 5\r\n"
         "\r\n"
         "HELLO"
  ,.should_keep_alive= FALSE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 0
  ,.method= HTTP_GET
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/get_funky_content_length_body_hello"
  ,.request_url= "/get_funky_content_length_body_hello"
  ,.num_headers= 1
  ,.headers=
    { { "conTENT-Length" , "5" }
    }
  ,.body= "HELLO"
  }

#define POST_IDENTITY_BODY_WORLD 7
, {.name= "post identity body world"
  ,.type= HTTP_REQUEST
  ,.raw= "POST /post_identity_body_world?q=search#hey HTTP/1.1\r\n"
         "Accept: */*\r\n"
         "Transfer-Encoding: identity\r\n"
         "Content-Length: 5\r\n"
         "\r\n"
         "World"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_POST
  ,.query_string= "q=search"
  ,.fragment= "hey"
  ,.request_path= "/post_identity_body_world"
  ,.request_url= "/post_identity_body_world?q=search#hey"
  ,.num_headers= 3
  ,.headers=
    { { "Accept", "*/*" }
    , { "Transfer-Encoding", "identity" }
    , { "Content-Length", "5" }
    }
  ,.body= "World"
  }

#define POST_CHUNKED_ALL_YOUR_BASE 8
, {.name= "post - chunked body: all your base are belong to us"
  ,.type= HTTP_REQUEST
  ,.raw= "POST /post_chunked_all_your_base HTTP/1.1\r\n"
         "Transfer-Encoding: chunked\r\n"
         "\r\n"
         "1e\r\nall your base are belong to us\r\n"
         "0\r\n"
         "\r\n"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_POST
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/post_chunked_all_your_base"
  ,.request_url= "/post_chunked_all_your_base"
  ,.num_headers= 1
  ,.headers=
    { { "Transfer-Encoding" , "chunked" }
    }
  ,.body= "all your base are belong to us"
  ,.num_chunks_complete= 2
  ,.chunk_lengths= { 0x1e }
  }

#define TWO_CHUNKS_MULT_ZERO_END 9
, {.name= "two chunks ; triple zero ending"
  ,.type= HTTP_REQUEST
  ,.raw= "POST /two_chunks_mult_zero_end HTTP/1.1\r\n"
         "Transfer-Encoding: chunked\r\n"
         "\r\n"
         "5\r\nhello\r\n"
         "6\r\n world\r\n"
         "000\r\n"
         "\r\n"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_POST
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/two_chunks_mult_zero_end"
  ,.request_url= "/two_chunks_mult_zero_end"
  ,.num_headers= 1
  ,.headers=
    { { "Transfer-Encoding", "chunked" }
    }
  ,.body= "hello world"
  ,.num_chunks_complete= 3
  ,.chunk_lengths= { 5, 6 }
  }

#define CHUNKED_W_TRAILING_HEADERS 10
, {.name= "chunked with trailing headers. blech."
  ,.type= HTTP_REQUEST
  ,.raw= "POST /chunked_w_trailing_headers HTTP/1.1\r\n"
         "Transfer-Encoding: chunked\r\n"
         "\r\n"
         "5\r\nhello\r\n"
         "6\r\n world\r\n"
         "0\r\n"
         "Vary: *\r\n"
         "Content-Type: text/plain\r\n"
         "\r\n"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_POST
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/chunked_w_trailing_headers"
  ,.request_url= "/chunked_w_trailing_headers"
  ,.num_headers= 3
  ,.headers=
    { { "Transfer-Encoding",  "chunked" }
    , { "Vary", "*" }
    , { "Content-Type", "text/plain" }
    }
  ,.body= "hello world"
  ,.num_chunks_complete= 3
  ,.chunk_lengths= { 5, 6 }
  }

#define CHUNKED_W_BULLSHIT_AFTER_LENGTH 11
, {.name= "with bullshit after the length"
  ,.type= HTTP_REQUEST
  ,.raw= "POST /chunked_w_bullshit_after_length HTTP/1.1\r\n"
         "Transfer-Encoding: chunked\r\n"
         "\r\n"
         "5; ihatew3;whatthefuck=aretheseparametersfor\r\nhello\r\n"
         "6; blahblah; blah\r\n world\r\n"
         "0\r\n"
         "\r\n"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_POST
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/chunked_w_bullshit_after_length"
  ,.request_url= "/chunked_w_bullshit_after_length"
  ,.num_headers= 1
  ,.headers=
    { { "Transfer-Encoding", "chunked" }
    }
  ,.body= "hello world"
  ,.num_chunks_complete= 3
  ,.chunk_lengths= { 5, 6 }
  }

#define WITH_QUOTES 12
, {.name= "with quotes"
  ,.type= HTTP_REQUEST
  ,.raw= "GET /with_\"stupid\"_quotes?foo=\"bar\" HTTP/1.1\r\n\r\n"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_GET
  ,.query_string= "foo=\"bar\""
  ,.fragment= ""
  ,.request_path= "/with_\"stupid\"_quotes"
  ,.request_url= "/with_\"stupid\"_quotes?foo=\"bar\""
  ,.num_headers= 0
  ,.headers= { }
  ,.body= ""
  }

#define APACHEBENCH_GET 13
/* The server receiving this request SHOULD NOT wait for EOF
 * to know that content-length == 0.
 * How to represent this in a unit test? message_complete_on_eof
 * Compare with NO_CONTENT_LENGTH_RESPONSE.
 */
, {.name = "apachebench get"
  ,.type= HTTP_REQUEST
  ,.raw= "GET /test HTTP/1.0\r\n"
         "Host: 0.0.0.0:5000\r\n"
         "User-Agent: ApacheBench/2.3\r\n"
         "Accept: */*\r\n\r\n"
  ,.should_keep_alive= FALSE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 0
  ,.method= HTTP_GET
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/test"
  ,.request_url= "/test"
  ,.num_headers= 3
  ,.headers= { { "Host", "0.0.0.0:5000" }
             , { "User-Agent", "ApacheBench/2.3" }
             , { "Accept", "*/*" }
             }
  ,.body= ""
  }

#define QUERY_URL_WITH_QUESTION_MARK_GET 14
/* Some clients include '?' characters in query strings.
 */
, {.name = "query url with question mark"
  ,.type= HTTP_REQUEST
  ,.raw= "GET /test.cgi?foo=bar?baz HTTP/1.1\r\n\r\n"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_GET
  ,.query_string= "foo=bar?baz"
  ,.fragment= ""
  ,.request_path= "/test.cgi"
  ,.request_url= "/test.cgi?foo=bar?baz"
  ,.num_headers= 0
  ,.headers= {}
  ,.body= ""
  }

#define PREFIX_NEWLINE_GET 15
/* Some clients, especially after a POST in a keep-alive connection,
 * will send an extra CRLF before the next request
 */
, {.name = "newline prefix get"
  ,.type= HTTP_REQUEST
  ,.raw= "\r\nGET /test HTTP/1.1\r\n\r\n"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_GET
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/test"
  ,.request_url= "/test"
  ,.num_headers= 0
  ,.headers= { }
  ,.body= ""
  }

#define UPGRADE_REQUEST 16
, {.name = "upgrade request"
  ,.type= HTTP_REQUEST
  ,.raw= "GET /demo HTTP/1.1\r\n"
         "Host: example.com\r\n"
         "Connection: Upgrade\r\n"
         "Sec-WebSocket-Key2: 12998 5 Y3 1  .P00\r\n"
         "Sec-WebSocket-Protocol: sample\r\n"
         "Upgrade: WebSocket\r\n"
         "Sec-WebSocket-Key1: 4 @1  46546xW%0l 1 5\r\n"
         "Origin: http://example.com\r\n"
         "\r\n"
         "Hot diggity dogg"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_GET
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/demo"
  ,.request_url= "/demo"
  ,.num_headers= 7
  ,.upgrade="Hot diggity dogg"
  ,.headers= { { "Host", "example.com" }
             , { "Connection", "Upgrade" }
             , { "Sec-WebSocket-Key2", "12998 5 Y3 1  .P00" }
             , { "Sec-WebSocket-Protocol", "sample" }
             , { "Upgrade", "WebSocket" }
             , { "Sec-WebSocket-Key1", "4 @1  46546xW%0l 1 5" }
             , { "Origin", "http://example.com" }
             }
  ,.body= ""
  }

#define CONNECT_REQUEST 17
, {.name = "connect request"
  ,.type= HTTP_REQUEST
  ,.raw= "CONNECT 0-home0.netscape.com:443 HTTP/1.0\r\n"
         "User-agent: Mozilla/1.1N\r\n"
         "Proxy-authorization: basic aGVsbG86d29ybGQ=\r\n"
         "\r\n"
         "some data\r\n"
         "and yet even more data"
  ,.should_keep_alive= FALSE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 0
  ,.method= HTTP_CONNECT
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= ""
  ,.request_url= "0-home0.netscape.com:443"
  ,.num_headers= 2
  ,.upgrade="some data\r\nand yet even more data"
  ,.headers= { { "User-agent", "Mozilla/1.1N" }
             , { "Proxy-authorization", "basic aGVsbG86d29ybGQ=" }
             }
  ,.body= ""
  }

#define REPORT_REQ 18
, {.name= "report request"
  ,.type= HTTP_REQUEST
  ,.raw= "REPORT /test HTTP/1.1\r\n"
         "\r\n"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_REPORT
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/test"
  ,.request_url= "/test"
  ,.num_headers= 0
  ,.headers= {}
  ,.body= ""
  }

#define NO_HTTP_VERSION 19
, {.name= "request with no http version"
  ,.type= HTTP_REQUEST
  ,.raw= "GET /\r\n"
         "\r\n"
  ,.should_keep_alive= FALSE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 0
  ,.http_minor= 9
  ,.method= HTTP_GET
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/"
  ,.request_url= "/"
  ,.num_headers= 0
  ,.headers= {}
  ,.body= ""
  }

#define MSEARCH_REQ 20
, {.name= "m-search request"
  ,.type= HTTP_REQUEST
  ,.raw= "M-SEARCH * HTTP/1.1\r\n"
         "HOST: 239.255.255.250:1900\r\n"
         "MAN: \"ssdp:discover\"\r\n"
         "ST: \"ssdp:all\"\r\n"
         "\r\n"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_MSEARCH
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "*"
  ,.request_url= "*"
  ,.num_headers= 3
  ,.headers= { { "HOST", "239.255.255.250:1900" }
             , { "MAN", "\"ssdp:discover\"" }
             , { "ST", "\"ssdp:all\"" }
             }
  ,.body= ""
  }

#define LINE_FOLDING_IN_HEADER 21
, {.name= "line folding in header value"
  ,.type= HTTP_REQUEST
  ,.raw= "GET / HTTP/1.1\r\n"
         "Line1:   abc\r\n"
         "\tdef\r\n"
         " ghi\r\n"
         "\t\tjkl\r\n"
         "  mno \r\n"
         "\t \tqrs\r\n"
         "Line2: \t line2\t\r\n"
         "Line3:\r\n"
         " line3\r\n"
         "Line4: \r\n"
         " \r\n"
         "Connection:\r\n"
         " close\r\n"
         "\r\n"
  ,.should_keep_alive= FALSE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_GET
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/"
  ,.request_url= "/"
  ,.num_headers= 5
  ,.headers= { { "Line1", "abc\tdef ghi\t\tjkl  mno \t \tqrs" }
             , { "Line2", "line2\t" }
             , { "Line3", "line3" }
             , { "Line4", "" }
             , { "Connection", "close" },
             }
  ,.body= ""
  }


#define QUERY_TERMINATED_HOST 22
, {.name= "host terminated by a query string"
  ,.type= HTTP_REQUEST
  ,.raw= "GET http://hypnotoad.org?hail=all HTTP/1.1\r\n"
         "\r\n"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_GET
  ,.query_string= "hail=all"
  ,.fragment= ""
  ,.request_path= ""
  ,.request_url= "http://hypnotoad.org?hail=all"
  ,.host= "hypnotoad.org"
  ,.num_headers= 0
  ,.headers= { }
  ,.body= ""
  }

#define QUERY_TERMINATED_HOSTPORT 23
, {.name= "host:port terminated by a query string"
  ,.type= HTTP_REQUEST
  ,.raw= "GET http://hypnotoad.org:1234?hail=all HTTP/1.1\r\n"
         "\r\n"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_GET
  ,.query_string= "hail=all"
  ,.fragment= ""
  ,.request_path= ""
  ,.request_url= "http://hypnotoad.org:1234?hail=all"
  ,.host= "hypnotoad.org"
  ,.port= 1234
  ,.num_headers= 0
  ,.headers= { }
  ,.body= ""
  }

#define SPACE_TERMINATED_HOSTPORT 24
, {.name= "host:port terminated by a space"
  ,.type= HTTP_REQUEST
  ,.raw= "GET http://hypnotoad.org:1234 HTTP/1.1\r\n"
         "\r\n"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_GET
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= ""
  ,.request_url= "http://hypnotoad.org:1234"
  ,.host= "hypnotoad.org"
  ,.port= 1234
  ,.num_headers= 0
  ,.headers= { }
  ,.body= ""
  }

#define PATCH_REQ 25
, {.name = "PATCH request"
  ,.type= HTTP_REQUEST
  ,.raw= "PATCH /file.txt HTTP/1.1\r\n"
         "Host: www.example.com\r\n"
         "Content-Type: application/example\r\n"
         "If-Match: \"e0023aa4e\"\r\n"
         "Content-Length: 10\r\n"
         "\r\n"
         "cccccccccc"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_PATCH
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/file.txt"
  ,.request_url= "/file.txt"
  ,.num_headers= 4
  ,.headers= { { "Host", "www.example.com" }
             , { "Content-Type", "application/example" }
             , { "If-Match", "\"e0023aa4e\"" }
             , { "Content-Length", "10" }
             }
  ,.body= "cccccccccc"
  }

#define CONNECT_CAPS_REQUEST 26
, {.name = "connect caps request"
  ,.type= HTTP_REQUEST
  ,.raw= "CONNECT HOME0.NETSCAPE.COM:443 HTTP/1.0\r\n"
         "User-agent: Mozilla/1.1N\r\n"
         "Proxy-authorization: basic aGVsbG86d29ybGQ=\r\n"
         "\r\n"
  ,.should_keep_alive= FALSE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 0
  ,.method= HTTP_CONNECT
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= ""
  ,.request_url= "HOME0.NETSCAPE.COM:443"
  ,.num_headers= 2
  ,.upgrade=""
  ,.headers= { { "User-agent", "Mozilla/1.1N" }
             , { "Proxy-authorization", "basic aGVsbG86d29ybGQ=" }
             }
  ,.body= ""
  }

#if !HTTP_PARSER_STRICT
#define UTF8_PATH_REQ 27
, {.name= "utf-8 path request"
  ,.type= HTTP_REQUEST
  ,.raw= "GET /δ¶/δt/pope?q=1#narf HTTP/1.1\r\n"
         "Host: github.com\r\n"
         "\r\n"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_GET
  ,.query_string= "q=1"
  ,.fragment= "narf"
  ,.request_path= "/δ¶/δt/pope"
  ,.request_url= "/δ¶/δt/pope?q=1#narf"
  ,.num_headers= 1
  ,.headers= { {"Host", "github.com" }
             }
  ,.body= ""
  }

#define HOSTNAME_UNDERSCORE 28
, {.name = "hostname underscore"
  ,.type= HTTP_REQUEST
  ,.raw= "CONNECT home_0.netscape.com:443 HTTP/1.0\r\n"
         "User-agent: Mozilla/1.1N\r\n"
         "Proxy-authorization: basic aGVsbG86d29ybGQ=\r\n"
         "\r\n"
  ,.should_keep_alive= FALSE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 0
  ,.method= HTTP_CONNECT
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= ""
  ,.request_url= "home_0.netscape.com:443"
  ,.num_headers= 2
  ,.upgrade=""
  ,.headers= { { "User-agent", "Mozilla/1.1N" }
             , { "Proxy-authorization", "basic aGVsbG86d29ybGQ=" }
             }
  ,.body= ""
  }
#endif  /* !HTTP_PARSER_STRICT */

/* see https://github.com/ry/http-parser/issues/47 */
#define EAT_TRAILING_CRLF_NO_CONNECTION_CLOSE 29
, {.name = "eat CRLF between requests, no \"Connection: close\" header"
  ,.raw= "POST / HTTP/1.1\r\n"
         "Host: www.example.com\r\n"
         "Content-Type: application/x-www-form-urlencoded\r\n"
         "Content-Length: 4\r\n"
         "\r\n"
         "q=42\r\n" /* note the trailing CRLF */
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_POST
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/"
  ,.request_url= "/"
  ,.num_headers= 3
  ,.upgrade= 0
  ,.headers= { { "Host", "www.example.com" }
             , { "Content-Type", "application/x-www-form-urlencoded" }
             , { "Content-Length", "4" }
             }
  ,.body= "q=42"
  }

/* see https://github.com/ry/http-parser/issues/47 */
#define EAT_TRAILING_CRLF_WITH_CONNECTION_CLOSE 30
, {.name = "eat CRLF between requests even if \"Connection: close\" is set"
  ,.raw= "POST / HTTP/1.1\r\n"
         "Host: www.example.com\r\n"
         "Content-Type: application/x-www-form-urlencoded\r\n"
         "Content-Length: 4\r\n"
         "Connection: close\r\n"
         "\r\n"
         "q=42\r\n" /* note the trailing CRLF */
  ,.should_keep_alive= FALSE
  ,.message_complete_on_eof= FALSE /* input buffer isn't empty when on_message_complete is called */
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_POST
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/"
  ,.request_url= "/"
  ,.num_headers= 4
  ,.upgrade= 0
  ,.headers= { { "Host", "www.example.com" }
             , { "Content-Type", "application/x-www-form-urlencoded" }
             , { "Content-Length", "4" }
             , { "Connection", "close" }
             }
  ,.body= "q=42"
  }

#define PURGE_REQ 31
, {.name = "PURGE request"
  ,.type= HTTP_REQUEST
  ,.raw= "PURGE /file.txt HTTP/1.1\r\n"
         "Host: www.example.com\r\n"
         "\r\n"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_PURGE
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/file.txt"
  ,.request_url= "/file.txt"
  ,.num_headers= 1
  ,.headers= { { "Host", "www.example.com" } }
  ,.body= ""
  }

#define SEARCH_REQ 32
, {.name = "SEARCH request"
  ,.type= HTTP_REQUEST
  ,.raw= "SEARCH / HTTP/1.1\r\n"
         "Host: www.example.com\r\n"
         "\r\n"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_SEARCH
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/"
  ,.request_url= "/"
  ,.num_headers= 1
  ,.headers= { { "Host", "www.example.com" } }
  ,.body= ""
  }

#define PROXY_WITH_BASIC_AUTH 33
, {.name= "host:port and basic_auth"
  ,.type= HTTP_REQUEST
  ,.raw= "GET http://a%12:b!&*$@hypnotoad.org:1234/toto HTTP/1.1\r\n"
         "\r\n"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_GET
  ,.fragment= ""
  ,.request_path= "/toto"
  ,.request_url= "http://a%12:b!&*$@hypnotoad.org:1234/toto"
  ,.host= "hypnotoad.org"
  ,.userinfo= "a%12:b!&*$"
  ,.port= 1234
  ,.num_headers= 0
  ,.headers= { }
  ,.body= ""
  }

#define LINE_FOLDING_IN_HEADER_WITH_LF 34
, {.name= "line folding in header value"
  ,.type= HTTP_REQUEST
  ,.raw= "GET / HTTP/1.1\n"
         "Line1:   abc\n"
         "\tdef\n"
         " ghi\n"
         "\t\tjkl\n"
         "  mno \n"
         "\t \tqrs\n"
         "Line2: \t line2\t\n"
         "Line3:\n"
         " line3\n"
         "Line4: \n"
         " \n"
         "Connection:\n"
         " close\n"
         "\n"
  ,.should_keep_alive= FALSE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_GET
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/"
  ,.request_url= "/"
  ,.num_headers= 5
  ,.headers= { { "Line1", "abc\tdef ghi\t\tjkl  mno \t \tqrs" }
             , { "Line2", "line2\t" }
             , { "Line3", "line3" }
             , { "Line4", "" }
             , { "Connection", "close" },
             }
  ,.body= ""
  }

#define CONNECTION_MULTI 35
, {.name = "multiple connection header values with folding"
  ,.type= HTTP_REQUEST
  ,.raw= "GET /demo HTTP/1.1\r\n"
         "Host: example.com\r\n"
         "Connection: Something,\r\n"
         " Upgrade, ,Keep-Alive\r\n"
         "Sec-WebSocket-Key2: 12998 5 Y3 1  .P00\r\n"
         "Sec-WebSocket-Protocol: sample\r\n"
         "Upgrade: WebSocket\r\n"
         "Sec-WebSocket-Key1: 4 @1  46546xW%0l 1 5\r\n"
         "Origin: http://example.com\r\n"
         "\r\n"
         "Hot diggity dogg"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_GET
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/demo"
  ,.request_url= "/demo"
  ,.num_headers= 7
  ,.upgrade="Hot diggity dogg"
  ,.headers= { { "Host", "example.com" }
             , { "Connection", "Something, Upgrade, ,Keep-Alive" }
             , { "Sec-WebSocket-Key2", "12998 5 Y3 1  .P00" }
             , { "Sec-WebSocket-Protocol", "sample" }
             , { "Upgrade", "WebSocket" }
             , { "Sec-WebSocket-Key1", "4 @1  46546xW%0l 1 5" }
             , { "Origin", "http://example.com" }
             }
  ,.body= ""
  }

#define CONNECTION_MULTI_LWS 36
, {.name = "multiple connection header values with folding and lws"
  ,.type= HTTP_REQUEST
  ,.raw= "GET /demo HTTP/1.1\r\n"
         "Connection: keep-alive, upgrade\r\n"
         "Upgrade: WebSocket\r\n"
         "\r\n"
         "Hot diggity dogg"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_GET
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/demo"
  ,.request_url= "/demo"
  ,.num_headers= 2
  ,.upgrade="Hot diggity dogg"
  ,.headers= { { "Connection", "keep-alive, upgrade" }
             , { "Upgrade", "WebSocket" }
             }
  ,.body= ""
  }

#define CONNECTION_MULTI_LWS_CRLF 37
, {.name = "multiple connection header values with folding and lws"
  ,.type= HTTP_REQUEST
  ,.raw= "GET /demo HTTP/1.1\r\n"
         "Connection: keep-alive, \r\n upgrade\r\n"
         "Upgrade: WebSocket\r\n"
         "\r\n"
         "Hot diggity dogg"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_GET
  ,.query_string= ""
  ,.fragment= ""
  ,.request_path= "/demo"
  ,.request_url= "/demo"
  ,.num_headers= 2
  ,.upgrade="Hot diggity dogg"
  ,.headers= { { "Connection", "keep-alive,  upgrade" }
             , { "Upgrade", "WebSocket" }
             }
  ,.body= ""
  }

#define UPGRADE_POST_REQUEST 38
, {.name = "upgrade post request"
  ,.type= HTTP_REQUEST
  ,.raw= "POST /demo HTTP/1.1\r\n"
         "Host: example.com\r\n"
         "Connection: Upgrade\r\n"
         "Upgrade: HTTP/2.0\r\n"
         "Content-Length: 15\r\n"
         "\r\n"
         "sweet post body"
         "Hot diggity dogg"
  ,.should_keep_alive= TRUE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 1
  ,.method= HTTP_POST
  ,.request_path= "/demo"
  ,.request_url= "/demo"
  ,.num_headers= 4
  ,.upgrade="Hot diggity dogg"
  ,.headers= { { "Host", "example.com" }
             , { "Connection", "Upgrade" }
             , { "Upgrade", "HTTP/2.0" }
             , { "Content-Length", "15" }
             }
  ,.body= "sweet post body"
  }

#define CONNECT_WITH_BODY_REQUEST 39
, {.name = "connect with body request"
  ,.type= HTTP_REQUEST
  ,.raw= "CONNECT foo.bar.com:443 HTTP/1.0\r\n"
         "User-agent: Mozilla/1.1N\r\n"
         "Proxy-authorization: basic aGVsbG86d29ybGQ=\r\n"
         "Content-Length: 10\r\n"
         "\r\n"
         "blarfcicle"
  ,.should_keep_alive= FALSE
  ,.message_complete_on_eof= FALSE
  ,.http_major= 1
  ,.http_minor= 0
  ,.method= HTTP_CONNECT
  ,.request_url= "foo.bar.com:443"
  ,.num_headers= 3
  ,.upgrade="blarfcicle"
  ,.headers= { { "User-agent", "Mozilla/1.1N" }
             , { "Proxy-authorization", "basic aGVsbG86d29ybGQ=" }
             , { "Content-Length", "10" }
             }
  ,.body= ""
  }

, {.name= NULL } /* sentinel */
};

/* strnlen() is a POSIX.2008 addition. Can't rely on it being available so
 * define it ourselves.
 */
size_t
strnlen(const char *s, size_t maxlen)
{
  const char *p;

  p = memchr(s, '\0', maxlen);
  if (p == NULL)
    return maxlen;

  return p - s;
}

size_t
strlncat(char *dst, size_t len, const char *src, size_t n)
{
  size_t slen;
  size_t dlen;
  size_t rlen;
  size_t ncpy;

  slen = strnlen(src, n);
  dlen = strnlen(dst, len);

  if (dlen < len) {
    rlen = len - dlen;
    ncpy = slen < rlen ? slen : (rlen - 1);
    memcpy(dst + dlen, src, ncpy);
    dst[dlen + ncpy] = '\0';
  }

  assert(len > slen + dlen);
  return slen + dlen;
}

size_t
strlcat(char *dst, const char *src, size_t len)
{
  return strlncat(dst, len, src, (size_t) -1);
}

size_t
strlncpy(char *dst, size_t len, const char *src, size_t n)
{
  size_t slen;
  size_t ncpy;

  slen = strnlen(src, n);

  if (len > 0) {
    ncpy = slen < len ? slen : (len - 1);
    memcpy(dst, src, ncpy);
    dst[ncpy] = '\0';
  }

  assert(len > slen);
  return slen;
}

size_t
strlcpy(char *dst, const char *src, size_t len)
{
  return strlncpy(dst, len, src, (size_t) -1);
}

int
request_url_cb (http_parser *p, const char *buf, size_t len)
{
  assert(p == parser);
  strlncat(messages[num_messages].request_url,
           sizeof(messages[num_messages].request_url),
           buf,
           len);
  return 0;
}

int
header_field_cb (http_parser *p, const char *buf, size_t len)
{
  assert(p == parser);
  struct message *m = &messages[num_messages];

  if (m->last_header_element != FIELD)
    m->num_headers++;

  strlncat(m->headers[m->num_headers-1][0],
           sizeof(m->headers[m->num_headers-1][0]),
           buf,
           len);

  m->last_header_element = FIELD;

  return 0;
}

int
header_value_cb (http_parser *p, const char *buf, size_t len)
{
  assert(p == parser);
  struct message *m = &messages[num_messages];

  strlncat(m->headers[m->num_headers-1][1],
           sizeof(m->headers[m->num_headers-1][1]),
           buf,
           len);

  m->last_header_element = VALUE;

  return 0;
}

void
check_body_is_final (const http_parser *p)
{
  if (messages[num_messages].body_is_final) {
    fprintf(stderr, "\n\n *** Error http_body_is_final() should return 1 "
                    "on last on_body callback call "
                    "but it doesn't! ***\n\n");
    assert(0);
    abort();
  }
  messages[num_messages].body_is_final = http_body_is_final(p);
}

int
body_cb (http_parser *p, const char *buf, size_t len)
{
  assert(p == parser);
  strlncat(messages[num_messages].body,
           sizeof(messages[num_messages].body),
           buf,
           len);
  messages[num_messages].body_size += len;
  check_body_is_final(p);
 // printf("body_cb: '%s'\n", requests[num_messages].body);
  return 0;
}

int
count_body_cb (http_parser *p, const char *buf, size_t len)
{
  assert(p == parser);
  assert(buf);
  messages[num_messages].body_size += len;
  check_body_is_final(p);
  return 0;
}

int
message_begin_cb (http_parser *p)
{
  assert(p == parser);
  messages[num_messages].message_begin_cb_called = TRUE;
  return 0;
}

int
headers_complete_cb (http_parser *p)
{
  assert(p == parser);
  messages[num_messages].method = parser->method;
  messages[num_messages].status_code = parser->status_code;
  messages[num_messages].http_major = parser->http_major;
  messages[num_messages].http_minor = parser->http_minor;
  messages[num_messages].headers_complete_cb_called = TRUE;
  messages[num_messages].should_keep_alive = http_should_keep_alive(parser);
  return 0;
}

int
message_complete_cb (http_parser *p)
{
  assert(p == parser);
  if (messages[num_messages].should_keep_alive != http_should_keep_alive(parser))
  {
    fprintf(stderr, "\n\n *** Error http_should_keep_alive() should have same "
                    "value in both on_message_complete and on_headers_complete "
                    "but it doesn't! ***\n\n");
    assert(0);
    abort();
  }

  if (messages[num_messages].body_size &&
      http_body_is_final(p) &&
      !messages[num_messages].body_is_final)
  {
    fprintf(stderr, "\n\n *** Error http_body_is_final() should return 1 "
                    "on last on_body callback call "
                    "but it doesn't! ***\n\n");
    assert(0);
    abort();
  }

  messages[num_messages].message_complete_cb_called = TRUE;

  messages[num_messages].message_complete_on_eof = currently_parsing_eof;

  num_messages++;
  return 0;
}

int
response_status_cb (http_parser *p, const char *buf, size_t len)
{
  assert(p == parser);
  strlncat(messages[num_messages].response_status,
           sizeof(messages[num_messages].response_status),
           buf,
           len);
  return 0;
}

int
chunk_header_cb (http_parser *p)
{
  assert(p == parser);
  int chunk_idx = messages[num_messages].num_chunks;
  messages[num_messages].num_chunks++;
  if (chunk_idx < MAX_CHUNKS) {
    messages[num_messages].chunk_lengths[chunk_idx] = p->content_length;
  }

  return 0;
}

int
chunk_complete_cb (http_parser *p)
{
  assert(p == parser);

  /* Here we want to verify that each chunk_header_cb is matched by a
   * chunk_complete_cb, so not only should the total number of calls to
   * both callbacks be the same, but they also should be interleaved
   * properly */
  assert(messages[num_messages].num_chunks ==
         messages[num_messages].num_chunks_complete + 1);

  messages[num_messages].num_chunks_complete++;
  return 0;
}

/* These dontcall_* callbacks exist so that we can verify that when we're
 * paused, no additional callbacks are invoked */
int
dontcall_message_begin_cb (http_parser *p)
{
  if (p) { } // gcc
  fprintf(stderr, "\n\n*** on_message_begin() called on paused parser ***\n\n");
  abort();
}

int
dontcall_header_field_cb (http_parser *p, const char *buf, size_t len)
{
  if (p || buf || len) { } // gcc
  fprintf(stderr, "\n\n*** on_header_field() called on paused parser ***\n\n");
  abort();
}

int
dontcall_header_value_cb (http_parser *p, const char *buf, size_t len)
{
  if (p || buf || len) { } // gcc
  fprintf(stderr, "\n\n*** on_header_value() called on paused parser ***\n\n");
  abort();
}

int
dontcall_request_url_cb (http_parser *p, const char *buf, size_t len)
{
  if (p || buf || len) { } // gcc
  fprintf(stderr, "\n\n*** on_request_url() called on paused parser ***\n\n");
  abort();
}

int
dontcall_body_cb (http_parser *p, const char *buf, size_t len)
{
  if (p || buf || len) { } // gcc
  fprintf(stderr, "\n\n*** on_body_cb() called on paused parser ***\n\n");
  abort();
}

int
dontcall_headers_complete_cb (http_parser *p)
{
  if (p) { } // gcc
  fprintf(stderr, "\n\n*** on_headers_complete() called on paused "
                  "parser ***\n\n");
  abort();
}

int
dontcall_message_complete_cb (http_parser *p)
{
  if (p) { } // gcc
  fprintf(stderr, "\n\n*** on_message_complete() called on paused "
                  "parser ***\n\n");
  abort();
}

int
dontcall_response_status_cb (http_parser *p, const char *buf, size_t len)
{
  if (p || buf || len) { } // gcc
  fprintf(stderr, "\n\n*** on_status() called on paused parser ***\n\n");
  abort();
}

int
dontcall_chunk_header_cb (http_parser *p)
{
  if (p) { } // gcc
  fprintf(stderr, "\n\n*** on_chunk_header() called on paused parser ***\n\n");
  exit(1);
}

int
dontcall_chunk_complete_cb (http_parser *p)
{
  if (p) { } // gcc
  fprintf(stderr, "\n\n*** on_chunk_complete() "
          "called on paused parser ***\n\n");
  exit(1);
}

static http_parser_settings settings_dontcall =
  {.on_message_begin = dontcall_message_begin_cb
  ,.on_header_field = dontcall_header_field_cb
  ,.on_header_value = dontcall_header_value_cb
  ,.on_url = dontcall_request_url_cb
  ,.on_status = dontcall_response_status_cb
  ,.on_body = dontcall_body_cb
  ,.on_headers_complete = dontcall_headers_complete_cb
  ,.on_message_complete = dontcall_message_complete_cb
  ,.on_chunk_header = dontcall_chunk_header_cb
  ,.on_chunk_complete = dontcall_chunk_complete_cb
  };

/* These pause_* callbacks always pause the parser and just invoke the regular
 * callback that tracks content. Before returning, we overwrite the parser
 * settings to point to the _dontcall variety so that we can verify that
 * the pause actually did, you know, pause. */
int
pause_message_begin_cb (http_parser *p)
{
  http_parser_pause(p, 1);
  *current_pause_parser = settings_dontcall;
  return message_begin_cb(p);
}

int
pause_header_field_cb (http_parser *p, const char *buf, size_t len)
{
  http_parser_pause(p, 1);
  *current_pause_parser = settings_dontcall;
  return header_field_cb(p, buf, len);
}

int
pause_header_value_cb (http_parser *p, const char *buf, size_t len)
{
  http_parser_pause(p, 1);
  *current_pause_parser = settings_dontcall;
  return header_value_cb(p, buf, len);
}

int
pause_request_url_cb (http_parser *p, const char *buf, size_t len)
{
  http_parser_pause(p, 1);
  *current_pause_parser = settings_dontcall;
  return request_url_cb(p, buf, len);
}

int
pause_body_cb (http_parser *p, const char *buf, size_t len)
{
  http_parser_pause(p, 1);
  *current_pause_parser = settings_dontcall;
  return body_cb(p, buf, len);
}

int
pause_headers_complete_cb (http_parser *p)
{
  http_parser_pause(p, 1);
  *current_pause_parser = settings_dontcall;
  return headers_complete_cb(p);
}

int
pause_message_complete_cb (http_parser *p)
{
  http_parser_pause(p, 1);
  *current_pause_parser = settings_dontcall;
  return message_complete_cb(p);
}

int
pause_response_status_cb (http_parser *p, const char *buf, size_t len)
{
  http_parser_pause(p, 1);
  *current_pause_parser = settings_dontcall;
  return response_status_cb(p, buf, len);
}

int
pause_chunk_header_cb (http_parser *p)
{
  http_parser_pause(p, 1);
  *current_pause_parser = settings_dontcall;
  return chunk_header_cb(p);
}

int
pause_chunk_complete_cb (http_parser *p)
{
  http_parser_pause(p, 1);
  *current_pause_parser = settings_dontcall;
  return chunk_complete_cb(p);
}

static http_parser_settings settings_pause =
  {.on_message_begin = pause_message_begin_cb
  ,.on_header_field = pause_header_field_cb
  ,.on_header_value = pause_header_value_cb
  ,.on_url = pause_request_url_cb
  ,.on_status = pause_response_status_cb
  ,.on_body = pause_body_cb
  ,.on_headers_complete = pause_headers_complete_cb
  ,.on_message_complete = pause_message_complete_cb
  ,.on_chunk_header = pause_chunk_header_cb
  ,.on_chunk_complete = pause_chunk_complete_cb
  };

static http_parser_settings settings =
  {.on_message_begin = message_begin_cb
  ,.on_header_field = header_field_cb
  ,.on_header_value = header_value_cb
  ,.on_url = request_url_cb
  ,.on_status = response_status_cb
  ,.on_body = body_cb
  ,.on_headers_complete = headers_complete_cb
  ,.on_message_complete = message_complete_cb
  ,.on_chunk_header = chunk_header_cb
  ,.on_chunk_complete = chunk_complete_cb
  };

static http_parser_settings settings_count_body =
  {.on_message_begin = message_begin_cb
  ,.on_header_field = header_field_cb
  ,.on_header_value = header_value_cb
  ,.on_url = request_url_cb
  ,.on_status = response_status_cb
  ,.on_body = count_body_cb
  ,.on_headers_complete = headers_complete_cb
  ,.on_message_complete = message_complete_cb
  ,.on_chunk_header = chunk_header_cb
  ,.on_chunk_complete = chunk_complete_cb
  };

static http_parser_settings settings_null =
  {.on_message_begin = 0
  ,.on_header_field = 0
  ,.on_header_value = 0
  ,.on_url = 0
  ,.on_status = 0
  ,.on_body = 0
  ,.on_headers_complete = 0
  ,.on_message_complete = 0
  ,.on_chunk_header = 0
  ,.on_chunk_complete = 0
  };

void
parser_init (enum http_parser_type type)
{
  num_messages = 0;

  assert(parser == NULL);

  parser = malloc(sizeof(http_parser));

  http_parser_init(parser, type);

  memset(&messages, 0, sizeof messages);

}

void
parser_free ()
{
  assert(parser);
  free(parser);
  parser = NULL;
}

size_t parse (const char *buf, size_t len)
{
  size_t nparsed;
  currently_parsing_eof = (len == 0);
  nparsed = http_parser_execute(parser, &settings, buf, len);
  return nparsed;
}

size_t parse_count_body (const char *buf, size_t len)
{
  size_t nparsed;
  currently_parsing_eof = (len == 0);
  nparsed = http_parser_execute(parser, &settings_count_body, buf, len);
  return nparsed;
}

size_t parse_pause (const char *buf, size_t len)
{
  size_t nparsed;
  http_parser_settings s = settings_pause;

  currently_parsing_eof = (len == 0);
  current_pause_parser = &s;
  nparsed = http_parser_execute(parser, current_pause_parser, buf, len);
  return nparsed;
}

static inline int
check_str_eq (const struct message *m,
              const char *prop,
              const char *expected,
              const char *found) {
  if ((expected == NULL) != (found == NULL)) {
    printf("\n*** Error: %s in '%s' ***\n\n", prop, m->name);
    printf("expected %s\n", (expected == NULL) ? "NULL" : expected);
    printf("   found %s\n", (found == NULL) ? "NULL" : found);
    return 0;
  }
  if (expected != NULL && 0 != strcmp(expected, found)) {
    printf("\n*** Error: %s in '%s' ***\n\n", prop, m->name);
    printf("expected '%s'\n", expected);
    printf("   found '%s'\n", found);
    return 0;
  }
  return 1;
}

static inline int
check_num_eq (const struct message *m,
              const char *prop,
              int expected,
              int found) {
  if (expected != found) {
    printf("\n*** Error: %s in '%s' ***\n\n", prop, m->name);
    printf("expected %d\n", expected);
    printf("   found %d\n", found);
    return 0;
  }
  return 1;
}

#define MESSAGE_CHECK_STR_EQ(expected, found, prop) \
  if (!check_str_eq(expected, #prop, expected->prop, found->prop)) return 0

#define MESSAGE_CHECK_NUM_EQ(expected, found, prop) \
  if (!check_num_eq(expected, #prop, expected->prop, found->prop)) return 0

#define MESSAGE_CHECK_URL_EQ(u, expected, found, prop, fn)           \
do {                                                                 \
  char ubuf[256];                                                    \
                                                                     \
  if ((u)->field_set & (1 << (fn))) {                                \
    memcpy(ubuf, (found)->request_url + (u)->field_data[(fn)].off,   \
      (u)->field_data[(fn)].len);                                    \
    ubuf[(u)->field_data[(fn)].len] = '\0';                          \
  } else {                                                           \
    ubuf[0] = '\0';                                                  \
  }                                                                  \
                                                                     \
  check_str_eq(expected, #prop, expected->prop, ubuf);               \
} while(0)

int
message_eq (int index, const struct message *expected)
{
  int i;
  struct message *m = &messages[index];

  MESSAGE_CHECK_NUM_EQ(expected, m, http_major);
  MESSAGE_CHECK_NUM_EQ(expected, m, http_minor);

  if (expected->type == HTTP_REQUEST) {
    MESSAGE_CHECK_NUM_EQ(expected, m, method);
  } else {
    MESSAGE_CHECK_NUM_EQ(expected, m, status_code);
    MESSAGE_CHECK_STR_EQ(expected, m, response_status);
  }

  MESSAGE_CHECK_NUM_EQ(expected, m, should_keep_alive);
  MESSAGE_CHECK_NUM_EQ(expected, m, message_complete_on_eof);

  assert(m->message_begin_cb_called);
  assert(m->headers_complete_cb_called);
  assert(m->message_complete_cb_called);


  MESSAGE_CHECK_STR_EQ(expected, m, request_url);

  /* Check URL components; we can't do this w/ CONNECT since it doesn't
   * send us a well-formed URL.
   */
  if (*m->request_url && m->method != HTTP_CONNECT) {
    struct http_parser_url u;

    if (http_parser_parse_url(m->request_url, strlen(m->request_url), 0, &u)) {
      fprintf(stderr, "\n\n*** failed to parse URL %s ***\n\n",
        m->request_url);
      abort();
    }

    if (expected->host) {
      MESSAGE_CHECK_URL_EQ(&u, expected, m, host, UF_HOST);
    }

    if (expected->userinfo) {
      MESSAGE_CHECK_URL_EQ(&u, expected, m, userinfo, UF_USERINFO);
    }

    m->port = (u.field_set & (1 << UF_PORT)) ?
      u.port : 0;

    MESSAGE_CHECK_URL_EQ(&u, expected, m, query_string, UF_QUERY);
    MESSAGE_CHECK_URL_EQ(&u, expected, m, fragment, UF_FRAGMENT);
    MESSAGE_CHECK_URL_EQ(&u, expected, m, request_path, UF_PATH);
    MESSAGE_CHECK_NUM_EQ(expected, m, port);
  }

  if (expected->body_size) {
    MESSAGE_CHECK_NUM_EQ(expected, m, body_size);
  } else {
    MESSAGE_CHECK_STR_EQ(expected, m, body);
  }

  assert(m->num_chunks == m->num_chunks_complete);
  MESSAGE_CHECK_NUM_EQ(expected, m, num_chunks_complete);
  for (i = 0; i < m->num_chunks && i < MAX_CHUNKS; i++) {
    MESSAGE_CHECK_NUM_EQ(expected, m, chunk_lengths[i]);
  }

  MESSAGE_CHECK_NUM_EQ(expected, m, num_headers);

  int r;
  for (i = 0; i < m->num_headers; i++) {
    r = check_str_eq(expected, "header field", expected->headers[i][0], m->headers[i][0]);
    if (!r) return 0;
    r = check_str_eq(expected, "header value", expected->headers[i][1], m->headers[i][1]);
    if (!r) return 0;
  }

  MESSAGE_CHECK_STR_EQ(expected, m, upgrade);

  return 1;
}

/* Given a sequence of varargs messages, return the number of them that the
 * parser should successfully parse, taking into account that upgraded
 * messages prevent all subsequent messages from being parsed.
 */
size_t
count_parsed_messages(const size_t nmsgs, ...) {
  size_t i;
  va_list ap;

  va_start(ap, nmsgs);

  for (i = 0; i < nmsgs; i++) {
    struct message *m = va_arg(ap, struct message *);

    if (m->upgrade) {
      va_end(ap);
      return i + 1;
    }
  }

  va_end(ap);
  return nmsgs;
}

/* Given a sequence of bytes and the number of these that we were able to
 * parse, verify that upgrade bodies are correct.
 */
void
upgrade_message_fix(char *body, const size_t nread, const size_t nmsgs, ...) {
  va_list ap;
  size_t i;
  size_t off = 0;
 
  va_start(ap, nmsgs);

  for (i = 0; i < nmsgs; i++) {
    struct message *m = va_arg(ap, struct message *);

    off += strlen(m->raw);

    if (m->upgrade) {
      off -= strlen(m->upgrade);

      /* Check the portion of the response after its specified upgrade */
      if (!check_str_eq(m, "upgrade", body + off, body + nread)) {
        abort();
      }

      /* Fix up the response so that message_eq() will verify the beginning
       * of the upgrade */
      *(body + nread + strlen(m->upgrade)) = '\0';
      messages[num_messages -1 ].upgrade = body + nread;

      va_end(ap);
      return;
    }
  }

  va_end(ap);
  printf("\n\n*** Error: expected a message with upgrade ***\n");

  abort();
}

static void
print_error (const char *raw, size_t error_location)
{
  fprintf(stderr, "\n*** %s ***\n\n",
          http_errno_description(HTTP_PARSER_ERRNO(parser)));

  int this_line = 0, char_len = 0;
  size_t i, j, len = strlen(raw), error_location_line = 0;
  for (i = 0; i < len; i++) {
    if (i == error_location) this_line = 1;
    switch (raw[i]) {
      case '\r':
        char_len = 2;
        fprintf(stderr, "\\r");
        break;

      case '\n':
        fprintf(stderr, "\\n\n");

        if (this_line) goto print;

        error_location_line = 0;
        continue;

      default:
        char_len = 1;
        fputc(raw[i], stderr);
        break;
    }
    if (!this_line) error_location_line += char_len;
  }

  fprintf(stderr, "[eof]\n");

 print:
  for (j = 0; j < error_location_line; j++) {
    fputc(' ', stderr);
  }
  fprintf(stderr, "^\n\nerror location: %u\n", (unsigned int)error_location);
}

void
test_preserve_data (void)
{
  char my_data[] = "application-specific data";
  http_parser parser;
  parser.data = my_data;
  http_parser_init(&parser, HTTP_REQUEST);
  if (parser.data != my_data) {
    printf("\n*** parser.data not preserved accross http_parser_init ***\n\n");
    abort();
  }
}

struct url_test {
  const char *name;
  const char *url;
  int is_connect;
  struct http_parser_url u;
  int rv;
};

const struct url_test url_tests[] =
{ {.name="proxy request"
  ,.url="http://hostname/"
  ,.is_connect=0
  ,.u=
    {.field_set=(1 << UF_SCHEMA) | (1 << UF_HOST) | (1 << UF_PATH)
    ,.port=0
    ,.field_data=
      {{  0,  4 } /* UF_SCHEMA */
      ,{  7,  8 } /* UF_HOST */
      ,{  0,  0 } /* UF_PORT */
      ,{ 15,  1 } /* UF_PATH */
      ,{  0,  0 } /* UF_QUERY */
      ,{  0,  0 } /* UF_FRAGMENT */
      ,{  0,  0 } /* UF_USERINFO */
      }
    }
  ,.rv=0
  }

, {.name="proxy request with port"
  ,.url="http://hostname:444/"
  ,.is_connect=0
  ,.u=
    {.field_set=(1 << UF_SCHEMA) | (1 << UF_HOST) | (1 << UF_PORT) | (1 << UF_PATH)
    ,.port=444
    ,.field_data=
      {{  0,  4 } /* UF_SCHEMA */
      ,{  7,  8 } /* UF_HOST */
      ,{ 16,  3 } /* UF_PORT */
      ,{ 19,  1 } /* UF_PATH */
      ,{  0,  0 } /* UF_QUERY */
      ,{  0,  0 } /* UF_FRAGMENT */
      ,{  0,  0 } /* UF_USERINFO */
      }
    }
  ,.rv=0
  }

, {.name="CONNECT request"
  ,.url="hostname:443"
  ,.is_connect=1
  ,.u=
    {.field_set=(1 << UF_HOST) | (1 << UF_PORT)
    ,.port=443
    ,.field_data=
      {{  0,  0 } /* UF_SCHEMA */
      ,{  0,  8 } /* UF_HOST */
      ,{  9,  3 } /* UF_PORT */
      ,{  0,  0 } /* UF_PATH */
      ,{  0,  0 } /* UF_QUERY */
      ,{  0,  0 } /* UF_FRAGMENT */
      ,{  0,  0 } /* UF_USERINFO */
      }
    }
  ,.rv=0
  }

, {.name="CONNECT request but not connect"
  ,.url="hostname:443"
  ,.is_connect=0
  ,.rv=1
  }

, {.name="proxy ipv6 request"
  ,.url="http://[1:2::3:4]/"
  ,.is_connect=0
  ,.u=
    {.field_set=(1 << UF_SCHEMA) | (1 << UF_HOST) | (1 << UF_PATH)
    ,.port=0
    ,.field_data=
      {{  0,  4 } /* UF_SCHEMA */
      ,{  8,  8 } /* UF_HOST */
      ,{  0,  0 } /* UF_PORT */
      ,{ 17,  1 } /* UF_PATH */
      ,{  0,  0 } /* UF_QUERY */
      ,{  0,  0 } /* UF_FRAGMENT */
      ,{  0,  0 } /* UF_USERINFO */
      }
    }
  ,.rv=0
  }

, {.name="proxy ipv6 request with port"
  ,.url="http://[1:2::3:4]:67/"
  ,.is_connect=0
  ,.u=
    {.field_set=(1 << UF_SCHEMA) | (1 << UF_HOST) | (1 << UF_PORT) | (1 << UF_PATH)
    ,.port=67
    ,.field_data=
      {{  0,  4 } /* UF_SCHEMA */
      ,{  8,  8 } /* UF_HOST */
      ,{ 18,  2 } /* UF_PORT */
      ,{ 20,  1 } /* UF_PATH */
      ,{  0,  0 } /* UF_QUERY */
      ,{  0,  0 } /* UF_FRAGMENT */
      ,{  0,  0 } /* UF_USERINFO */
      }
    }
  ,.rv=0
  }

, {.name="CONNECT ipv6 address"
  ,.url="[1:2::3:4]:443"
  ,.is_connect=1
  ,.u=
    {.field_set=(1 << UF_HOST) | (1 << UF_PORT)
    ,.port=443
    ,.field_data=
      {{  0,  0 } /* UF_SCHEMA */
      ,{  1,  8 } /* UF_HOST */
      ,{ 11,  3 } /* UF_PORT */
      ,{  0,  0 } /* UF_PATH */
      ,{  0,  0 } /* UF_QUERY */
      ,{  0,  0 } /* UF_FRAGMENT */
      ,{  0,  0 } /* UF_USERINFO */
      }
    }
  ,.rv=0
  }

, {.name="ipv4 in ipv6 address"
  ,.url="http://[2001:0000:0000:0000:0000:0000:1.9.1.1]/"
  ,.is_connect=0
  ,.u=
    {.field_set=(1 << UF_SCHEMA) | (1 << UF_HOST) | (1 << UF_PATH)
    ,.port=0
    ,.field_data=
      {{  0,  4 } /* UF_SCHEMA */
      ,{  8, 37 } /* UF_HOST */
      ,{  0,  0 } /* UF_PORT */
      ,{ 46,  1 } /* UF_PATH */
      ,{  0,  0 } /* UF_QUERY */
      ,{  0,  0 } /* UF_FRAGMENT */
      ,{  0,  0 } /* UF_USERINFO */
      }
    }
  ,.rv=0
  }

, {.name="extra ? in query string"
  ,.url="http://a.tbcdn.cn/p/fp/2010c/??fp-header-min.css,fp-base-min.css,"
  "fp-channel-min.css,fp-product-min.css,fp-mall-min.css,fp-category-min.css,"
  "fp-sub-min.css,fp-gdp4p-min.css,fp-css3-min.css,fp-misc-min.css?t=20101022.css"
  ,.is_connect=0
  ,.u=
    {.field_set=(1<<UF_SCHEMA) | (1<<UF_HOST) | (1<<UF_PATH) | (1<<UF_QUERY)
    ,.port=0
    ,.field_data=
      {{  0,  4 } /* UF_SCHEMA */
      ,{  7, 10 } /* UF_HOST */
      ,{  0,  0 } /* UF_PORT */
      ,{ 17, 12 } /* UF_PATH */
      ,{ 30,187 } /* UF_QUERY */
      ,{  0,  0 } /* UF_FRAGMENT */
      ,{  0,  0 } /* UF_USERINFO */
      }
    }
  ,.rv=0
  }

, {.name="space URL encoded"
  ,.url="/toto.html?toto=a%20b"
  ,.is_connect=0
  ,.u=
    {.field_set= (1<<UF_PATH) | (1<<UF_QUERY)
    ,.port=0
    ,.field_data=
      {{  0,  0 } /* UF_SCHEMA */
      ,{  0,  0 } /* UF_HOST */
      ,{  0,  0 } /* UF_PORT */
      ,{  0, 10 } /* UF_PATH */
      ,{ 11, 10 } /* UF_QUERY */
      ,{  0,  0 } /* UF_FRAGMENT */
      ,{  0,  0 } /* UF_USERINFO */
      }
    }
  ,.rv=0
  }


, {.name="URL fragment"
  ,.url="/toto.html#titi"
  ,.is_connect=0
  ,.u=
    {.field_set= (1<<UF_PATH) | (1<<UF_FRAGMENT)
    ,.port=0
    ,.field_data=
      {{  0,  0 } /* UF_SCHEMA */
      ,{  0,  0 } /* UF_HOST */
      ,{  0,  0 } /* UF_PORT */
      ,{  0, 10 } /* UF_PATH */
      ,{  0,  0 } /* UF_QUERY */
      ,{ 11,  4 } /* UF_FRAGMENT */
      ,{  0,  0 } /* UF_USERINFO */
      }
    }
  ,.rv=0
  }

, {.name="complex URL fragment"
  ,.url="http://www.webmasterworld.com/r.cgi?f=21&d=8405&url="
    "http://www.example.com/index.html?foo=bar&hello=world#midpage"
  ,.is_connect=0
  ,.u=
    {.field_set= (1<<UF_SCHEMA) | (1<<UF_HOST) | (1<<UF_PATH) | (1<<UF_QUERY) |\
      (1<<UF_FRAGMENT)
    ,.port=0
    ,.field_data=
      {{  0,  4 } /* UF_SCHEMA */
      ,{  7, 22 } /* UF_HOST */
      ,{  0,  0 } /* UF_PORT */
      ,{ 29,  6 } /* UF_PATH */
      ,{ 36, 69 } /* UF_QUERY */
      ,{106,  7 } /* UF_FRAGMENT */
      ,{  0,  0 } /* UF_USERINFO */
      }
    }
  ,.rv=0
  }

, {.name="complex URL from node js url parser doc"
  ,.url="http://host.com:8080/p/a/t/h?query=string#hash"
  ,.is_connect=0
  ,.u=
    {.field_set= (1<<UF_SCHEMA) | (1<<UF_HOST) | (1<<UF_PORT) | (1<<UF_PATH) |\
      (1<<UF_QUERY) | (1<<UF_FRAGMENT)
    ,.port=8080
    ,.field_data=
      {{  0,  4 } /* UF_SCHEMA */
      ,{  7,  8 } /* UF_HOST */
      ,{ 16,  4 } /* UF_PORT */
      ,{ 20,  8 } /* UF_PATH */
      ,{ 29, 12 } /* UF_QUERY */
      ,{ 42,  4 } /* UF_FRAGMENT */
      ,{  0,  0 } /* UF_USERINFO */
      }
    }
  ,.rv=0
  }

, {.name="complex URL with basic auth from node js url parser doc"
  ,.url="http://a:b@host.com:8080/p/a/t/h?query=string#hash"
  ,.is_connect=0
  ,.u=
    {.field_set= (1<<UF_SCHEMA) | (1<<UF_HOST) | (1<<UF_PORT) | (1<<UF_PATH) |\
      (1<<UF_QUERY) | (1<<UF_FRAGMENT) | (1<<UF_USERINFO)
    ,.port=8080
    ,.field_data=
      {{  0,  4 } /* UF_SCHEMA */
      ,{ 11,  8 } /* UF_HOST */
      ,{ 20,  4 } /* UF_PORT */
      ,{ 24,  8 } /* UF_PATH */
      ,{ 33, 12 } /* UF_QUERY */
      ,{ 46,  4 } /* UF_FRAGMENT */
      ,{  7,  3 } /* UF_USERINFO */
      }
    }
  ,.rv=0
  }

, {.name="double @"
  ,.url="http://a:b@@hostname:443/"
  ,.is_connect=0
  ,.rv=1
  }

, {.name="proxy empty host"
  ,.url="http://:443/"
  ,.is_connect=0
  ,.rv=1
  }

, {.name="proxy empty port"
  ,.url="http://hostname:/"
  ,.is_connect=0
  ,.rv=1
  }

, {.name="CONNECT with basic auth"
  ,.url="a:b@hostname:443"
  ,.is_connect=1
  ,.rv=1
  }

, {.name="CONNECT empty host"
  ,.url=":443"
  ,.is_connect=1
  ,.rv=1
  }

, {.name="CONNECT empty port"
  ,.url="hostname:"
  ,.is_connect=1
  ,.rv=1
  }

, {.name="CONNECT with extra bits"
  ,.url="hostname:443/"
  ,.is_connect=1
  ,.rv=1
  }

, {.name="space in URL"
  ,.url="/foo bar/"
  ,.rv=1 /* s_dead */
  }

, {.name="proxy basic auth with space url encoded"
  ,.url="http://a%20:b@host.com/"
  ,.is_connect=0
  ,.u=
    {.field_set= (1<<UF_SCHEMA) | (1<<UF_HOST) | (1<<UF_PATH) | (1<<UF_USERINFO)
    ,.port=0
    ,.field_data=
      {{  0,  4 } /* UF_SCHEMA */
      ,{ 14,  8 } /* UF_HOST */
      ,{  0,  0 } /* UF_PORT */
      ,{ 22,  1 } /* UF_PATH */
      ,{  0,  0 } /* UF_QUERY */
      ,{  0,  0 } /* UF_FRAGMENT */
      ,{  7,  6 } /* UF_USERINFO */
      }
    }
  ,.rv=0
  }

, {.name="carriage return in URL"
  ,.url="/foo\rbar/"
  ,.rv=1 /* s_dead */
  }

, {.name="proxy double : in URL"
  ,.url="http://hostname::443/"
  ,.rv=1 /* s_dead */
  }

, {.name="proxy basic auth with double :"
  ,.url="http://a::b@host.com/"
  ,.is_connect=0
  ,.u=
    {.field_set= (1<<UF_SCHEMA) | (1<<UF_HOST) | (1<<UF_PATH) | (1<<UF_USERINFO)
    ,.port=0
    ,.field_data=
      {{  0,  4 } /* UF_SCHEMA */
      ,{ 12,  8 } /* UF_HOST */
      ,{  0,  0 } /* UF_PORT */
      ,{ 20,  1 } /* UF_PATH */
      ,{  0,  0 } /* UF_QUERY */
      ,{  0,  0 } /* UF_FRAGMENT */
      ,{  7,  4 } /* UF_USERINFO */
      }
    }
  ,.rv=0
  }

, {.name="line feed in URL"
  ,.url="/foo\nbar/"
  ,.rv=1 /* s_dead */
  }

, {.name="proxy empty basic auth"
  ,.url="http://@hostname/fo"
  ,.u=
    {.field_set= (1<<UF_SCHEMA) | (1<<UF_HOST) | (1<<UF_PATH)
    ,.port=0
    ,.field_data=
      {{  0,  4 } /* UF_SCHEMA */
      ,{  8,  8 } /* UF_HOST */
      ,{  0,  0 } /* UF_PORT */
      ,{ 16,  3 } /* UF_PATH */
      ,{  0,  0 } /* UF_QUERY */
      ,{  0,  0 } /* UF_FRAGMENT */
      ,{  0,  0 } /* UF_USERINFO */
      }
    }
  ,.rv=0
  }
, {.name="proxy line feed in hostname"
  ,.url="http://host\name/fo"
  ,.rv=1 /* s_dead */
  }

, {.name="proxy % in hostname"
  ,.url="http://host%name/fo"
  ,.rv=1 /* s_dead */
  }

, {.name="proxy ; in hostname"
  ,.url="http://host;ame/fo"
  ,.rv=1 /* s_dead */
  }

, {.name="proxy basic auth with unreservedchars"
  ,.url="http://a!;-_!=+$@host.com/"
  ,.is_connect=0
  ,.u=
    {.field_set= (1<<UF_SCHEMA) | (1<<UF_HOST) | (1<<UF_PATH) | (1<<UF_USERINFO)
    ,.port=0
    ,.field_data=
      {{  0,  4 } /* UF_SCHEMA */
      ,{ 17,  8 } /* UF_HOST */
      ,{  0,  0 } /* UF_PORT */
      ,{ 25,  1 } /* UF_PATH */
      ,{  0,  0 } /* UF_QUERY */
      ,{  0,  0 } /* UF_FRAGMENT */
      ,{  7,  9 } /* UF_USERINFO */
      }
    }
  ,.rv=0
  }

, {.name="proxy only empty basic auth"
  ,.url="http://@/fo"
  ,.rv=1 /* s_dead */
  }

, {.name="proxy only basic auth"
  ,.url="http://toto@/fo"
  ,.rv=1 /* s_dead */
  }

, {.name="proxy emtpy hostname"
  ,.url="http:///fo"
  ,.rv=1 /* s_dead */
  }

, {.name="proxy = in URL"
  ,.url="http://host=ame/fo"
  ,.rv=1 /* s_dead */
  }

#if HTTP_PARSER_STRICT

, {.name="tab in URL"
  ,.url="/foo\tbar/"
  ,.rv=1 /* s_dead */
  }

, {.name="form feed in URL"
  ,.url="/foo\fbar/"
  ,.rv=1 /* s_dead */
  }

#else /* !HTTP_PARSER_STRICT */

, {.name="tab in URL"
  ,.url="/foo\tbar/"
  ,.u=
    {.field_set=(1 << UF_PATH)
    ,.field_data=
      {{  0,  0 } /* UF_SCHEMA */
      ,{  0,  0 } /* UF_HOST */
      ,{  0,  0 } /* UF_PORT */
      ,{  0,  9 } /* UF_PATH */
      ,{  0,  0 } /* UF_QUERY */
      ,{  0,  0 } /* UF_FRAGMENT */
      ,{  0,  0 } /* UF_USERINFO */
      }
    }
  ,.rv=0
  }

, {.name="form feed in URL"
  ,.url="/foo\fbar/"
  ,.u=
    {.field_set=(1 << UF_PATH)
    ,.field_data=
      {{  0,  0 } /* UF_SCHEMA */
      ,{  0,  0 } /* UF_HOST */
      ,{  0,  0 } /* UF_PORT */
      ,{  0,  9 } /* UF_PATH */
      ,{  0,  0 } /* UF_QUERY */
      ,{  0,  0 } /* UF_FRAGMENT */
      ,{  0,  0 } /* UF_USERINFO */
      }
    }
  ,.rv=0
  }
#endif
};

void
dump_url (const char *url, const struct http_parser_url *u)
{
  unsigned int i;

  printf("\tfield_set: 0x%x, port: %u\n", u->field_set, u->port);
  for (i = 0; i < UF_MAX; i++) {
    if ((u->field_set & (1 << i)) == 0) {
      printf("\tfield_data[%u]: unset\n", i);
      continue;
    }

    printf("\tfield_data[%u]: off: %u len: %u part: \"%.*s\n\"",
           i,
           u->field_data[i].off,
           u->field_data[i].len,
           u->field_data[i].len,
           url + u->field_data[i].off);
  }
}

void
test_parse_url (void)
{
  struct http_parser_url u;
  const struct url_test *test;
  unsigned int i;
  int rv;

  for (i = 0; i < (sizeof(url_tests) / sizeof(url_tests[0])); i++) {
    test = &url_tests[i];
    memset(&u, 0, sizeof(u));

    rv = http_parser_parse_url(test->url,
                               strlen(test->url),
                               test->is_connect,
                               &u);

    if (test->rv == 0) {
      if (rv != 0) {
        printf("\n*** http_parser_parse_url(\"%s\") \"%s\" test failed, "
               "unexpected rv %d ***\n\n", test->url, test->name, rv);
        abort();
      }

      if (memcmp(&u, &test->u, sizeof(u)) != 0) {
        printf("\n*** http_parser_parse_url(\"%s\") \"%s\" failed ***\n",
               test->url, test->name);

        printf("target http_parser_url:\n");
        dump_url(test->url, &test->u);
        printf("result http_parser_url:\n");
        dump_url(test->url, &u);

        abort();
      }
    } else {
      /* test->rv != 0 */
      if (rv == 0) {
        printf("\n*** http_parser_parse_url(\"%s\") \"%s\" test failed, "
               "unexpected rv %d ***\n\n", test->url, test->name, rv);
        abort();
      }
    }
  }
}

void
test_method_str (void)
{
  assert(0 == strcmp("GET", http_method_str(HTTP_GET)));
  assert(0 == strcmp("<unknown>", http_method_str(1337)));
}

void
test_message (const struct message *message)
{
  size_t raw_len = strlen(message->raw);
  size_t msg1len;
  for (msg1len = 0; msg1len < raw_len; msg1len++) {
    parser_init(message->type);

    size_t read;
    const char *msg1 = message->raw;
    const char *msg2 = msg1 + msg1len;
    size_t msg2len = raw_len - msg1len;

    if (msg1len) {
      read = parse(msg1, msg1len);

      if (message->upgrade && parser->upgrade && num_messages > 0) {
        messages[num_messages - 1].upgrade = msg1 + read;
        goto test;
      }

      if (read != msg1len) {
        print_error(msg1, read);
        abort();
      }
    }


    read = parse(msg2, msg2len);

    if (message->upgrade && parser->upgrade) {
      messages[num_messages - 1].upgrade = msg2 + read;
      goto test;
    }

    if (read != msg2len) {
      print_error(msg2, read);
      abort();
    }

    read = parse(NULL, 0);

    if (read != 0) {
      print_error(message->raw, read);
      abort();
    }

  test:

    if (num_messages != 1) {
      printf("\n*** num_messages != 1 after testing '%s' ***\n\n", message->name);
      abort();
    }

    if(!message_eq(0, message)) abort();

    parser_free();
  }
}

void
test_message_count_body (const struct message *message)
{
  parser_init(message->type);

  size_t read;
  size_t l = strlen(message->raw);
  size_t i, toread;
  size_t chunk = 4024;

  for (i = 0; i < l; i+= chunk) {
    toread = MIN(l-i, chunk);
    read = parse_count_body(message->raw + i, toread);
    if (read != toread) {
      print_error(message->raw, read);
      abort();
    }
  }


  read = parse_count_body(NULL, 0);
  if (read != 0) {
    print_error(message->raw, read);
    abort();
  }

  if (num_messages != 1) {
    printf("\n*** num_messages != 1 after testing '%s' ***\n\n", message->name);
    abort();
  }

  if(!message_eq(0, message)) abort();

  parser_free();
}

void
test_simple (const char *buf, enum http_errno err_expected)
{
  parser_init(HTTP_REQUEST);

  enum http_errno err;

  parse(buf, strlen(buf));
  err = HTTP_PARSER_ERRNO(parser);
  parse(NULL, 0);

  parser_free();

  /* In strict mode, allow us to pass with an unexpected HPE_STRICT as
   * long as the caller isn't expecting success.
   */
#if HTTP_PARSER_STRICT
  if (err_expected != err && err_expected != HPE_OK && err != HPE_STRICT) {
#else
  if (err_expected != err) {
#endif
    fprintf(stderr, "\n*** test_simple expected %s, but saw %s ***\n\n%s\n",
        http_errno_name(err_expected), http_errno_name(err), buf);
    abort();
  }
}

void
test_header_overflow_error ()
{
  http_parser parser;
  http_parser_init(&parser, HTTP_REQUEST);
  size_t parsed;
  const char *buf;
  buf = "GET / HTTP/1.1\r\n";
  parsed = http_parser_execute(&parser, &settings_null, buf, strlen(buf));

  buf = "header-key: header-value\r\n";
  size_t buflen = strlen(buf);

  int i;
  for (i = 0; i < 10000; i++) {
    parsed = http_parser_execute(&parser, &settings_null, buf, buflen);
    if (parsed != buflen) {
      //fprintf(stderr, "error found on iter %d\n", i);
      assert(HTTP_PARSER_ERRNO(&parser) == HPE_HEADER_OVERFLOW);
      return;
    }
  }

  fprintf(stderr, "\n*** Error expected but none in header overflow test ***\n");
  abort();
}


void
test_header_nread_value ()
{
  http_parser parser;
  http_parser_init(&parser, HTTP_REQUEST);
  size_t parsed;
  const char *buf;
  buf = "GET / HTTP/1.1\r\nheader: value\nhdr: value\r\n";
  parsed = http_parser_execute(&parser, &settings_null, buf, strlen(buf));
  assert(parsed == strlen(buf));

  assert(parser.nread == strlen(buf));
}

void
test_no_overflow_long_body (size_t length)
{
  http_parser parser;
  http_parser_init(&parser, HTTP_REQUEST);
  size_t parsed;
  size_t i;
  char buf1[3000];
  size_t buf1len = sprintf(buf1, "%s\r\nConnection: Keep-Alive\r\nContent-Length: %lu\r\n\r\n",
      "POST / HTTP/1.0", (unsigned long)length);
  parsed = http_parser_execute(&parser, &settings_null, buf1, buf1len);
  if (parsed != buf1len)
    goto err;

  for (i = 0; i < length; i++) {
    char foo = 'a';
    parsed = http_parser_execute(&parser, &settings_null, &foo, 1);
    if (parsed != 1)
      goto err;
  }

  parsed = http_parser_execute(&parser, &settings_null, buf1, buf1len);
  if (parsed != buf1len) goto err;
  return;

 err:
  fprintf(stderr,
          "\n*** error in test_no_overflow_long_body %s of length %lu ***\n",
          "REQUEST",
          (unsigned long)length);
  abort();
}

void
test_multiple3 (const struct message *r1, const struct message *r2, const struct message *r3)
{
  int message_count = count_parsed_messages(3, r1, r2, r3);

  char total[ strlen(r1->raw)
            + strlen(r2->raw)
            + strlen(r3->raw)
            + 1
            ];
  total[0] = '\0';

  strcat(total, r1->raw);
  strcat(total, r2->raw);
  strcat(total, r3->raw);

  parser_init(r1->type);

  size_t read;

  read = parse(total, strlen(total));

  if (parser->upgrade) {
    upgrade_message_fix(total, read, 3, r1, r2, r3);
    goto test;
  }

  if (read != strlen(total)) {
    print_error(total, read);
    abort();
  }

  read = parse(NULL, 0);

  if (read != 0) {
    print_error(total, read);
    abort();
  }

test:

  if (message_count != num_messages) {
    fprintf(stderr, "\n\n*** Parser didn't see 3 messages only %d *** \n", num_messages);
    abort();
  }

  if (!message_eq(0, r1)) abort();
  if (message_count > 1 && !message_eq(1, r2)) abort();
  if (message_count > 2 && !message_eq(2, r3)) abort();

  parser_free();
}

/* SCAN through every possible breaking to make sure the
 * parser can handle getting the content in any chunks that
 * might come from the socket
 */
void
test_scan (const struct message *r1, const struct message *r2, const struct message *r3)
{
  char total[80*1024] = "\0";
  char buf1[80*1024] = "\0";
  char buf2[80*1024] = "\0";
  char buf3[80*1024] = "\0";

  strcat(total, r1->raw);
  strcat(total, r2->raw);
  strcat(total, r3->raw);

  size_t read;

  int total_len = strlen(total);

  int total_ops = 2 * (total_len - 1) * (total_len - 2) / 2;
  int ops = 0 ;

  size_t buf1_len, buf2_len, buf3_len;
  int message_count = count_parsed_messages(3, r1, r2, r3);

  int i,j,type_both;
  for (type_both = 0; type_both < 2; type_both ++ ) {
    for (j = 2; j < total_len; j ++ ) {
      for (i = 1; i < j; i ++ ) {

        if (ops % 1000 == 0)  {
          printf("\b\b\b\b%3.0f%%", 100 * (float)ops /(float)total_ops);
          fflush(stdout);
        }
        ops += 1;

        parser_init(HTTP_REQUEST);

        buf1_len = i;
        strlncpy(buf1, sizeof(buf1), total, buf1_len);
        buf1[buf1_len] = 0;

        buf2_len = j - i;
        strlncpy(buf2, sizeof(buf1), total+i, buf2_len);
        buf2[buf2_len] = 0;

        buf3_len = total_len - j;
        strlncpy(buf3, sizeof(buf1), total+j, buf3_len);
        buf3[buf3_len] = 0;

        read = parse(buf1, buf1_len);

        if (parser->upgrade) goto test;

        if (read != buf1_len) {
          print_error(buf1, read);
          goto error;
        }

        read += parse(buf2, buf2_len);

        if (parser->upgrade) goto test;

        if (read != buf1_len + buf2_len) {
          print_error(buf2, read);
          goto error;
        }

        read += parse(buf3, buf3_len);

        if (parser->upgrade) goto test;

        if (read != buf1_len + buf2_len + buf3_len) {
          print_error(buf3, read);
          goto error;
        }

        parse(NULL, 0);

test:
        if (parser->upgrade) {
          upgrade_message_fix(total, read, 3, r1, r2, r3);
        }

        if (message_count != num_messages) {
          fprintf(stderr, "\n\nParser didn't see %d messages only %d\n",
            message_count, num_messages);
          goto error;
        }

        if (!message_eq(0, r1)) {
          fprintf(stderr, "\n\nError matching messages[0] in test_scan.\n");
          goto error;
        }

        if (message_count > 1 && !message_eq(1, r2)) {
          fprintf(stderr, "\n\nError matching messages[1] in test_scan.\n");
          goto error;
        }

        if (message_count > 2 && !message_eq(2, r3)) {
          fprintf(stderr, "\n\nError matching messages[2] in test_scan.\n");
          goto error;
        }

        parser_free();
      }
    }
  }
  puts("\b\b\b\b100%");
  return;

 error:
  fprintf(stderr, "i=%d  j=%d\n", i, j);
  fprintf(stderr, "buf1 (%u) %s\n\n", (unsigned int)buf1_len, buf1);
  fprintf(stderr, "buf2 (%u) %s\n\n", (unsigned int)buf2_len , buf2);
  fprintf(stderr, "buf3 (%u) %s\n", (unsigned int)buf3_len, buf3);
  abort();
}

// user required to free the result
// string terminated by \0
char *
create_large_chunked_message (int body_size_in_kb, const char* headers)
{
  int i;
  size_t wrote = 0;
  size_t headers_len = strlen(headers);
  size_t bufsize = headers_len + (5+1024+2)*body_size_in_kb + 6;
  char * buf = malloc(bufsize);

  memcpy(buf, headers, headers_len);
  wrote += headers_len;

  for (i = 0; i < body_size_in_kb; i++) {
    // write 1kb chunk into the body.
    memcpy(buf + wrote, "400\r\n", 5);
    wrote += 5;
    memset(buf + wrote, 'C', 1024);
    wrote += 1024;
    strcpy(buf + wrote, "\r\n");
    wrote += 2;
  }

  memcpy(buf + wrote, "0\r\n\r\n", 6);
  wrote += 6;
  assert(wrote == bufsize);

  return buf;
}

/* Verify that we can pause parsing at any of the bytes in the
 * message and still get the result that we're expecting. */
void
test_message_pause (const struct message *msg)
{
  char *buf = (char*) msg->raw;
  size_t buflen = strlen(msg->raw);
  size_t nread;

  parser_init(msg->type);

  do {
    nread = parse_pause(buf, buflen);

    // We can only set the upgrade buffer once we've gotten our message
    // completion callback.
    if (messages[0].message_complete_cb_called &&
        msg->upgrade &&
        parser->upgrade) {
      messages[0].upgrade = buf + nread;
      goto test;
    }

    if (nread < buflen) {

      // Not much do to if we failed a strict-mode check
      if (HTTP_PARSER_ERRNO(parser) == HPE_STRICT) {
        parser_free();
        return;
      }

      assert (HTTP_PARSER_ERRNO(parser) == HPE_PAUSED);
    }

    buf += nread;
    buflen -= nread;
    http_parser_pause(parser, 0);
  } while (buflen > 0);

  nread = parse_pause(NULL, 0);
  assert (nread == 0);

test:
  if (num_messages != 1) {
    printf("\n*** num_messages != 1 after testing '%s' ***\n\n", msg->name);
    abort();
  }

  if(!message_eq(0, msg)) abort();

  parser_free();
}

int
main (void)
{
  parser = NULL;
  int i, j, k;
  int request_count;
  unsigned long version;
  unsigned major;
  unsigned minor;
  unsigned patch;

  version = http_parser_version();
  major = (version >> 16) & 255;
  minor = (version >> 8) & 255;
  patch = version & 255;
  printf("http_parser v%u.%u.%u (0x%06lx)\n", major, minor, patch, version);

  printf("sizeof(http_parser) = %u\n", (unsigned int)sizeof(http_parser));

  for (request_count = 0; requests[request_count].name; request_count++);

  //// API
  test_preserve_data();
  test_parse_url();
  test_method_str();

  //// NREAD
  test_header_nread_value();

  //// OVERFLOW CONDITIONS

  test_header_overflow_error();
  test_no_overflow_long_body(1000);
  test_no_overflow_long_body(100000);

  /// REQUESTS

  test_simple("GET / HTP/1.1\r\n\r\n", HPE_INVALID_VERSION);

  // Well-formed but incomplete
  test_simple("GET / HTTP/1.1\r\n"
              "Content-Type: text/plain\r\n"
              "Content-Length: 6\r\n"
              "\r\n"
              "fooba",
              HPE_OK);

  static const char *all_methods[] = {
    "DELETE",
    "GET",
    "HEAD",
    "POST",
    "PUT",
    //"CONNECT", //CONNECT can't be tested like other methods, it's a tunnel
    "OPTIONS",
    "TRACE",
    "COPY",
    "LOCK",
    "MKCOL",
    "MOVE",
    "PROPFIND",
    "PROPPATCH",
    "UNLOCK",
    "REPORT",
    "MKACTIVITY",
    "CHECKOUT",
    "MERGE",
    "M-SEARCH",
    "NOTIFY",
    "SUBSCRIBE",
    "UNSUBSCRIBE",
    "PATCH",
    0 };
  const char **this_method;
  for (this_method = all_methods; *this_method; this_method++) {
    char buf[200];
    sprintf(buf, "%s / HTTP/1.1\r\n\r\n", *this_method);
    test_simple(buf, HPE_OK);
  }

  static const char *bad_methods[] = {
      "ASDF",
      "C******",
      "COLA",
      "GEM",
      "GETA",
      "M****",
      "MKCOLA",
      "PROPPATCHA",
      "PUN",
      "PX",
      "SA",
      "hello world",
      0 };
  for (this_method = bad_methods; *this_method; this_method++) {
    char buf[200];
    sprintf(buf, "%s / HTTP/1.1\r\n\r\n", *this_method);
    test_simple(buf, HPE_INVALID_METHOD);
  }

  // illegal header field name line folding
  test_simple("GET / HTTP/1.1\r\n"
              "name\r\n"
              " : value\r\n"
              "\r\n",
              HPE_INVALID_HEADER_TOKEN);

  const char *dumbfuck2 =
    "GET / HTTP/1.1\r\n"
    "X-SSL-Bullshit:   -----BEGIN CERTIFICATE-----\r\n"
    "\tMIIFbTCCBFWgAwIBAgICH4cwDQYJKoZIhvcNAQEFBQAwcDELMAkGA1UEBhMCVUsx\r\n"
    "\tETAPBgNVBAoTCGVTY2llbmNlMRIwEAYDVQQLEwlBdXRob3JpdHkxCzAJBgNVBAMT\r\n"
    "\tAkNBMS0wKwYJKoZIhvcNAQkBFh5jYS1vcGVyYXRvckBncmlkLXN1cHBvcnQuYWMu\r\n"
    "\tdWswHhcNMDYwNzI3MTQxMzI4WhcNMDcwNzI3MTQxMzI4WjBbMQswCQYDVQQGEwJV\r\n"
    "\tSzERMA8GA1UEChMIZVNjaWVuY2UxEzARBgNVBAsTCk1hbmNoZXN0ZXIxCzAJBgNV\r\n"
    "\tBAcTmrsogriqMWLAk1DMRcwFQYDVQQDEw5taWNoYWVsIHBhcmQYJKoZIhvcNAQEB\r\n"
    "\tBQADggEPADCCAQoCggEBANPEQBgl1IaKdSS1TbhF3hEXSl72G9J+WC/1R64fAcEF\r\n"
    "\tW51rEyFYiIeZGx/BVzwXbeBoNUK41OK65sxGuflMo5gLflbwJtHBRIEKAfVVp3YR\r\n"
    "\tgW7cMA/s/XKgL1GEC7rQw8lIZT8RApukCGqOVHSi/F1SiFlPDxuDfmdiNzL31+sL\r\n"
    "\t0iwHDdNkGjy5pyBSB8Y79dsSJtCW/iaLB0/n8Sj7HgvvZJ7x0fr+RQjYOUUfrePP\r\n"
    "\tu2MSpFyf+9BbC/aXgaZuiCvSR+8Snv3xApQY+fULK/xY8h8Ua51iXoQ5jrgu2SqR\r\n"
    "\twgA7BUi3G8LFzMBl8FRCDYGUDy7M6QaHXx1ZWIPWNKsCAwEAAaOCAiQwggIgMAwG\r\n"
    "\tA1UdEwEB/wQCMAAwEQYJYIZIAYb4QgHTTPAQDAgWgMA4GA1UdDwEB/wQEAwID6DAs\r\n"
    "\tBglghkgBhvhCAQ0EHxYdVUsgZS1TY2llbmNlIFVzZXIgQ2VydGlmaWNhdGUwHQYD\r\n"
    "\tVR0OBBYEFDTt/sf9PeMaZDHkUIldrDYMNTBZMIGaBgNVHSMEgZIwgY+AFAI4qxGj\r\n"
    "\tloCLDdMVKwiljjDastqooXSkcjBwMQswCQYDVQQGEwJVSzERMA8GA1UEChMIZVNj\r\n"
    "\taWVuY2UxEjAQBgNVBAsTCUF1dGhvcml0eTELMAkGA1UEAxMCQ0ExLTArBgkqhkiG\r\n"
    "\t9w0BCQEWHmNhLW9wZXJhdG9yQGdyaWQtc3VwcG9ydC5hYy51a4IBADApBgNVHRIE\r\n"
    "\tIjAggR5jYS1vcGVyYXRvckBncmlkLXN1cHBvcnQuYWMudWswGQYDVR0gBBIwEDAO\r\n"
    "\tBgwrBgEEAdkvAQEBAQYwPQYJYIZIAYb4QgEEBDAWLmh0dHA6Ly9jYS5ncmlkLXN1\r\n"
    "\tcHBvcnQuYWMudmT4sopwqlBWsvcHViL2NybC9jYWNybC5jcmwwPQYJYIZIAYb4QgEDBDAWLmh0\r\n"
    "\tdHA6Ly9jYS5ncmlkLXN1cHBvcnQuYWMudWsvcHViL2NybC9jYWNybC5jcmwwPwYD\r\n"
    "\tVR0fBDgwNjA0oDKgMIYuaHR0cDovL2NhLmdyaWQt5hYy51ay9wdWIv\r\n"
    "\tY3JsL2NhY3JsLmNybDANBgkqhkiG9w0BAQUFAAOCAQEAS/U4iiooBENGW/Hwmmd3\r\n"
    "\tXCy6Zrt08YjKCzGNjorT98g8uGsqYjSxv/hmi0qlnlHs+k/3Iobc3LjS5AMYr5L8\r\n"
    "\tUO7OSkgFFlLHQyC9JzPfmLCAugvzEbyv4Olnsr8hbxF1MbKZoQxUZtMVu29wjfXk\r\n"
    "\thTeApBv7eaKCWpSp7MCbvgzm74izKhu3vlDk9w6qVrxePfGgpKPqfHiOoGhFnbTK\r\n"
    "\twTC6o2xq5y0qZ03JonF7OJspEd3I5zKY3E+ov7/ZhW6DqT8UFvsAdjvQbXyhV8Eu\r\n"
    "\tYhixw1aKEPzNjNowuIseVogKOLXxWI5vAi5HgXdS0/ES5gDGsABo4fqovUKlgop3\r\n"
    "\tRA==\r\n"
    "\t-----END CERTIFICATE-----\r\n"
    "\r\n";
  test_simple(dumbfuck2, HPE_OK);

  const char *corrupted_connection =
    "GET / HTTP/1.1\r\n"
    "Host: www.example.com\r\n"
    "Connection\r\033\065\325eep-Alive\r\n"
    "Accept-Encoding: gzip\r\n"
    "\r\n";
  test_simple(corrupted_connection, HPE_INVALID_HEADER_TOKEN);

  const char *corrupted_header_name =
    "GET / HTTP/1.1\r\n"
    "Host: www.example.com\r\n"
    "X-Some-Header\r\033\065\325eep-Alive\r\n"
    "Accept-Encoding: gzip\r\n"
    "\r\n";
  test_simple(corrupted_header_name, HPE_INVALID_HEADER_TOKEN);

#if 0
  // NOTE(Wed Nov 18 11:57:27 CET 2009) this seems okay. we just read body
  // until EOF.
  //
  // no content-length
  // error if there is a body without content length
  const char *bad_get_no_headers_no_body = "GET /bad_get_no_headers_no_body/world HTTP/1.1\r\n"
                                           "Accept: */*\r\n"
                                           "\r\n"
                                           "HELLO";
  test_simple(bad_get_no_headers_no_body, 0);
#endif
  /* TODO sending junk and large headers gets rejected */


  /* check to make sure our predefined requests are okay */
  for (i = 0; requests[i].name; i++) {
    test_message(&requests[i]);
  }

  for (i = 0; i < request_count; i++) {
    test_message_pause(&requests[i]);
  }

  for (i = 0; i < request_count; i++) {
    if (!requests[i].should_keep_alive) continue;
    for (j = 0; j < request_count; j++) {
      if (!requests[j].should_keep_alive) continue;
      for (k = 0; k < request_count; k++) {
        test_multiple3(&requests[i], &requests[j], &requests[k]);
      }
    }
  }

  printf("request scan 1/4      ");
  test_scan( &requests[GET_NO_HEADERS_NO_BODY]
           , &requests[GET_ONE_HEADER_NO_BODY]
           , &requests[GET_NO_HEADERS_NO_BODY]
           );

  printf("request scan 2/4      ");
  test_scan( &requests[POST_CHUNKED_ALL_YOUR_BASE]
           , &requests[POST_IDENTITY_BODY_WORLD]
           , &requests[GET_FUNKY_CONTENT_LENGTH]
           );

  printf("request scan 3/4      ");
  test_scan( &requests[TWO_CHUNKS_MULT_ZERO_END]
           , &requests[CHUNKED_W_TRAILING_HEADERS]
           , &requests[CHUNKED_W_BULLSHIT_AFTER_LENGTH]
           );

  printf("request scan 4/4      ");
  test_scan( &requests[QUERY_URL_WITH_QUESTION_MARK_GET]
           , &requests[PREFIX_NEWLINE_GET ]
           , &requests[CONNECT_REQUEST]
           );

  puts("requests okay");

  return 0;
}
