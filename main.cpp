/*
 * main.cpp
 *
 *  Created on: May 3, 2015
 *      Author: lotus
 */
#include <event.h>
#include <evhttp.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <iostream>

#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>


using namespace std;

namespace servers {
namespace util {


static const struct table_entry {
	const char *extension;
	const char *content_type;
} content_type_table[] = {
	{ "txt", "text/plain" },
	{ "c", "text/plain" },
	{ "h", "text/plain" },
	{ "cpp", "text/plain" },
	{ "hpp", "text/plain" },
	{ "html", "text/html" },
	{ "htm", "text/htm" },
	{ "css", "text/css" },
	{ "xml", "text/css" },
	{ "gif", "image/gif" },
	{ "jpg", "image/jpeg" },
	{ "jpeg", "image/jpeg" },
	{ "png", "image/png" },
	{ "pdf", "application/pdf" },
	{ "ps", "application/postsript" },
	{ NULL, NULL },
};

char uri_root[512];


class HTTPServer {
public:

  HTTPServer() {}
  ~HTTPServer() {}
  int serv(int port, int nthreads, void *arg);
protected:
  static void* Dispatch(void *arg);
  static void GenericHandler(struct evhttp_request *req, void *arg);
  void ProcessRequest(struct evhttp_request *request, void *arg);
  int BindSocket(int port);


  static const char * guess_content_type(const char *path)
  {
  	const char *last_period, *extension;
  	const struct table_entry *ent;
  	last_period = strrchr(path, '.');
  	if (!last_period || strchr(last_period, '/'))
  		goto not_found; /* no exension */
  	extension = last_period + 1;
  	for (ent = &content_type_table[0]; ent->extension; ++ent) {
  		if (!evutil_ascii_strcasecmp(ent->extension, extension))
  			return ent->content_type;
  	}

  not_found:
  	return "application/misc";
  }


  /* Callback used for the /dump URI, and for every non-GET request:
   * dumps all information to stdout and gives back a trivial 200 ok */
  static void dump_request_cb(struct evhttp_request *req, void *arg)
  {
  	const char *cmdtype;
  	struct evkeyvalq *headers;
  	struct evkeyval *header;
  	struct evbuffer *buf;

  	switch (evhttp_request_get_command(req)) {
  	case EVHTTP_REQ_GET: cmdtype = "GET"; break;
  	case EVHTTP_REQ_POST: cmdtype = "POST"; break;
  	case EVHTTP_REQ_HEAD: cmdtype = "HEAD"; break;
  	case EVHTTP_REQ_PUT: cmdtype = "PUT"; break;
  	case EVHTTP_REQ_DELETE: cmdtype = "DELETE"; break;
  	case EVHTTP_REQ_OPTIONS: cmdtype = "OPTIONS"; break;
  	case EVHTTP_REQ_TRACE: cmdtype = "TRACE"; break;
  	case EVHTTP_REQ_CONNECT: cmdtype = "CONNECT"; break;
  	case EVHTTP_REQ_PATCH: cmdtype = "PATCH"; break;
  	default: cmdtype = "unknown"; break;
  	}

  	printf("Received a %s request for %s\nHeaders:\n",
  	    cmdtype, evhttp_request_get_uri(req));

  	headers = evhttp_request_get_input_headers(req);
  	for (header = headers->tqh_first; header;
  	    header = header->next.tqe_next) {
  		printf("  %s: %s\n", header->key, header->value);
  	}

  	buf = evhttp_request_get_input_buffer(req);
  	puts("Input data: <<<");
  	while (evbuffer_get_length(buf)) {
  		int n;
  		char cbuf[128];
  		n = evbuffer_remove(buf, cbuf, sizeof(cbuf));
  		if (n > 0)
  			(void) fwrite(cbuf, 1, n, stdout);
  	}
  	puts(">>>");

  	evhttp_send_reply(req, 200, "OK", NULL);
  }



};

int HTTPServer::BindSocket(int port) {
  int r;
  int nfd;
  nfd = socket(AF_INET, SOCK_STREAM, 0);
  if (nfd < 0) return -1;

  int one = 1;
  r = setsockopt(nfd, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(int));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  r = bind(nfd, (struct sockaddr*)&addr, sizeof(addr));
  if (r < 0) return -1;
  r = listen(nfd, 10240);
  if (r < 0) return -1;

  int flags;
  if ((flags = fcntl(nfd, F_GETFL, 0)) < 0
      || fcntl(nfd, F_SETFL, flags | O_NONBLOCK) < 0)
    return -1;

  return nfd;
}

int HTTPServer::serv(int port, int nthreads, void *arg) {
  int r;
  int nfd = BindSocket(port);
  if (nfd < 0) return -1;
  pthread_t ths[nthreads];
  for (int i = 0; i < nthreads; i++) {
    struct event_base *base = event_init();
    if (base == NULL) return -1;
    struct evhttp *httpd = evhttp_new(base);
    if (httpd == NULL) return -1;
    r = evhttp_accept_socket(httpd, nfd);
    if (r != 0) return -1;
    //evhttp_set_gencb(httpd, HTTPServer::GenericHandler, this);
    evhttp_set_gencb(httpd, HTTPServer::GenericHandler, arg);
    r = pthread_create(&ths[i], NULL, HTTPServer::Dispatch, base);
    if (r != 0) return -1;
  }
  for (int i = 0; i < nthreads; i++) {
    pthread_join(ths[i], NULL);
  }
  return 0;
}

void* HTTPServer::Dispatch(void *arg) {
  event_base_dispatch((struct event_base*)arg);
  return NULL;
}

void HTTPServer::GenericHandler(struct evhttp_request *req, void *arg) {

  //((HTTPServer*)arg)->ProcessRequest(req, arg);

	struct evbuffer *evb = NULL;
	const char *docroot = (char *) arg;
	const char *uri = evhttp_request_get_uri(req);
	struct evhttp_uri *decoded = NULL;
	const char *path;
	char *decoded_path;
	char *whole_path = NULL;
	size_t len;
	int fd = -1;
	struct stat st;

	if (evhttp_request_get_command(req) != EVHTTP_REQ_GET) {
		dump_request_cb(req, arg);
		return;
	}

	printf("Got a GET request for <%s>\n",  uri);

	/* Decode the URI */
	decoded = evhttp_uri_parse(uri);
	if (!decoded) {
		printf("It's not a good URI. Sending BADREQUEST\n");
		evhttp_send_error(req, HTTP_BADREQUEST, 0);
		return;
	}

	/* Let's see what path the user asked for. */
	path = evhttp_uri_get_path(decoded);
	if (!path) path = "/";

	/* We need to decode it, to see what path the user really wanted. */
	decoded_path = evhttp_uridecode(path, 0, NULL);
	if (decoded_path == NULL)
		goto err;
	/* Don't allow any ".."s in the path, to avoid exposing stuff outside
	 * of the docroot.  This test is both overzealous and underzealous:
	 * it forbids aceptable paths like "/this/one..here", but it doesn't
	 * do anything to prevent symlink following." */
	if (strstr(decoded_path, ".."))
		goto err;

	len = strlen(decoded_path)+strlen(docroot)+2;
	if (!(whole_path =(char*) malloc(len))) {
		perror("malloc");
		goto err;
	}
	evutil_snprintf(whole_path, len, "%s/%s", docroot, decoded_path);

	if (stat(whole_path, &st)<0) {
		goto err;
	}

	/* This holds the content we're sending. */
	evb = evbuffer_new();

	if (S_ISDIR(st.st_mode)) {
		/* If it's a directory, read the comments and make a little
		 * index page */
		DIR *d;
		struct dirent *ent;
		const char *trailing_slash = "";

		if (!strlen(path) || path[strlen(path)-1] != '/')
			trailing_slash = "/";

		if (!(d = opendir(whole_path)))
			goto err;
		evbuffer_add_printf(evb, "<html>\n <head>\n"
		    "  <title>%s</title>\n"
		    "  <base href='%s%s%s'>\n"
		    " </head>\n"
		    " <body>\n"
		    "  <h1>%s</h1>\n"
		    "  <ul>\n",
		    decoded_path, /* XXX html-escape this. */
		    uri_root,
		    path, /* XXX html-escape this? */
		    trailing_slash,
		    decoded_path /* XXX html-escape this */);
		while ((ent = readdir(d))) {
			const char *name = ent->d_name;
			evbuffer_add_printf(evb,
			    "    <li><a href=\"%s\">%s</a>\n",
			    name, name);/* XXX escape this */
		}
		evbuffer_add_printf(evb, "</ul></body></html>\n");
		closedir(d);
		evhttp_add_header(evhttp_request_get_output_headers(req),
		    "Content-Type", "text/html");
	} else {
		/* Otherwise it's a file; add it to the buffer to get
		 * sent via sendfile */
		const char *type = guess_content_type(decoded_path);
		if ((fd = open(whole_path, O_RDONLY)) < 0) {
			perror("open");
			goto err;
		}

		if (fstat(fd, &st)<0) {
			/* Make sure the length still matches, now that we
			 * opened the file :/ */
			perror("fstat");
			goto err;
		}
		evhttp_add_header(evhttp_request_get_output_headers(req),
		    "Content-Type", type);
		evbuffer_add_file(evb, fd, 0, st.st_size);
	}

	evhttp_send_reply(req, 200, "OK", evb);
	goto done;
err:
	evhttp_send_error(req, 404, "Document was not found");
	if (fd>=0)
		close(fd);
done:
	if (decoded)
		evhttp_uri_free(decoded);
	if (decoded_path)
		free(decoded_path);
	if (whole_path)
		free(whole_path);
	if (evb)
		evbuffer_free(evb);
}

void HTTPServer::ProcessRequest(struct evhttp_request *req, void *arg) {
  sleep(1);
/*
  struct evbuffer *evb = evbuffer_new();
  if (evb == NULL) return;
  evbuffer_add_printf(evb, "Requested: %s\n", evhttp_request_uri(req));
  evhttp_send_reply(req, HTTP_OK, "OK", evb);
*/
	struct evbuffer *evb = NULL;
	const char *docroot = (char *) arg;
	const char *uri = evhttp_request_get_uri(req);
	struct evhttp_uri *decoded = NULL;
	const char *path;
	char *decoded_path;
	char *whole_path = NULL;
	size_t len;
	int fd = -1;
	struct stat st;

	if (evhttp_request_get_command(req) != EVHTTP_REQ_GET) {
		dump_request_cb(req, arg);
		return;
	}

	printf("Got a GET request for <%s>\n",  uri);

	/* Decode the URI */
	decoded = evhttp_uri_parse(uri);
	if (!decoded) {
		printf("It's not a good URI. Sending BADREQUEST\n");
		evhttp_send_error(req, HTTP_BADREQUEST, 0);
		return;
	}

	/* Let's see what path the user asked for. */
	path = evhttp_uri_get_path(decoded);
	if (!path) path = "/";

	/* We need to decode it, to see what path the user really wanted. */
	decoded_path = evhttp_uridecode(path, 0, NULL);
	if (decoded_path == NULL)
		goto err;
	/* Don't allow any ".."s in the path, to avoid exposing stuff outside
	 * of the docroot.  This test is both overzealous and underzealous:
	 * it forbids aceptable paths like "/this/one..here", but it doesn't
	 * do anything to prevent symlink following." */
	if (strstr(decoded_path, ".."))
		goto err;

	len = strlen(decoded_path)+strlen(docroot)+2;
	if (!(whole_path =(char*) malloc(len))) {
		perror("malloc");
		goto err;
	}
	evutil_snprintf(whole_path, len, "%s/%s", docroot, decoded_path);

	if (stat(whole_path, &st)<0) {
		goto err;
	}

	/* This holds the content we're sending. */
	evb = evbuffer_new();

	if (S_ISDIR(st.st_mode)) {
		/* If it's a directory, read the comments and make a little
		 * index page */
		DIR *d;
		struct dirent *ent;
		const char *trailing_slash = "";

		if (!strlen(path) || path[strlen(path)-1] != '/')
			trailing_slash = "/";

		if (!(d = opendir(whole_path)))
			goto err;

		evbuffer_add_printf(evb, "<html>\n <head>\n"
		    "  <title>%s</title>\n"
		    "  <base href='%s%s%s'>\n"
		    " </head>\n"
		    " <body>\n"
		    "  <h1>%s</h1>\n"
		    "  <ul>\n",
		    decoded_path, /* XXX html-escape this. */
		    uri_root, path, /* XXX html-escape this? */
		    trailing_slash,
		    decoded_path /* XXX html-escape this */);
		while ((ent = readdir(d))) {
			const char *name = ent->d_name;
			evbuffer_add_printf(evb,
			    "    <li><a href=\"%s\">%s</a>\n",
			    name, name);/* XXX escape this */
		}
		evbuffer_add_printf(evb, "</ul></body></html>\n");
		closedir(d);
		evhttp_add_header(evhttp_request_get_output_headers(req),
		    "Content-Type", "text/html");
	} else {
		/* Otherwise it's a file; add it to the buffer to get
		 * sent via sendfile */
		const char *type = guess_content_type(decoded_path);
		if ((fd = open(whole_path, O_RDONLY)) < 0) {
			perror("open");
			goto err;
		}

		if (fstat(fd, &st)<0) {
			/* Make sure the length still matches, now that we
			 * opened the file :/ */
			perror("fstat");
			goto err;
		}
		evhttp_add_header(evhttp_request_get_output_headers(req),
		    "Content-Type", type);
		evbuffer_add_file(evb, fd, 0, st.st_size);
	}

	evhttp_send_reply(req, 200, "OK", evb);
	goto done;
err:
	evhttp_send_error(req, 404, "Document was not found");
	if (fd>=0)
		close(fd);
done:
	if (decoded)
		evhttp_uri_free(decoded);
	if (decoded_path)
		free(decoded_path);
	if (whole_path)
		free(whole_path);
	if (evb)
		evbuffer_free(evb);
}

}
}

int main(int argc, char **argv) {
  servers::util::HTTPServer s;
  s.serv(4999, 10, argv[1]);
}


