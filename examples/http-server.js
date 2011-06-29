// This is a simple example of a HTTP server in Gjs using libsoup

const Lang = imports.lang;

const GLib = imports.gi.GLib;
const Soup = imports.gi.Soup;

function HTTPServer(args) {
  this._init(args);
}

HTTPServer.prototype = {
    _init : function(args) {
        this._handlers = [];
        this._port = 'port' in args ? args.port : 1080;
        this._server = this._startServer();
    },

    run : function() {
        this._server.run()
    },

    addHandler : function(path, handler) {
        this._handlers.push({ pathRegexp: new RegExp(path), handler : handler });
    },

    _startServer : function() {
        let server = new Soup.Server({ port: this._port});
        server.connect("request-started",
                       Lang.bind(this, this._onRequestStarted));
        server.connect("request-finished",
                       Lang.bind(this, this._onRequestFinished));
        return server;
    },

    _invokeHandlers : function(message) {
        let uri = message.uri;
        for (let i = 0; i < this._handlers.length; ++i) {
            let handlerGroup = this._handlers[i];
            if (uri.path.match(handlerGroup.pathRegexp)) {
                let request = new HTTPRequest({ uri: uri });
                let response = handlerGroup.handler(request);
                if (response !== undefined) {
                    this._setResponse(message, response);
                    return true;
                }
            }
        }
      return false;
    },

    _setResponse : function(message, response) {
        message.set_status(response.status);
        message.set_response(response.mimeType, Soup.MemoryUse.COPY,
                             response.content, response.content.length);
    },

   _onRequestStarted : function(server, message, context) {
        message._gotBodyId = message.connect("got-body",
                         Lang.bind(this, this._onMessageGotBody));
   },

    _onMessageGotBody : function(message) {
        if (!this._invokeHandlers(message)) {
            let error = new HTTPResponse("ERROR: Not found.");
            error.status = 404;
            this._setResponse(message, error);
        }
   },

   _onRequestFinished : function(server, message, context) {
        message.disconnect(message._gotBodyId);
   }
};

function HTTPRequest(args) {
   this._init(args);
};

HTTPRequest.prototype = {
   _init : function(args) {
      this._uri = args.uri;
   },

   toString : function() {
       return "[object HTTPRequest uri=" + this._uri.to_string(false) + "]";
   }

};

function HTTPResponse(content) {
   this._init(content);
};

HTTPResponse.prototype = {
   _init : function(content) {
      this._content = content
      this._status = 200;
      this._mimeType = "text/html";
   },

   toString : function() {
       return "[object HTTPResponse uri=" + this._content + "]";
   },

   get content() {
      return this._content;
   },

   get mimeType() {
      return this._mimeType;
   },

   set status(status) {
      this._status = status
   },

   get status() {
      return this._status;
   }
};

let main = function() {
   let handler = function(request) {
       return new HTTPResponse('Index page<br><a href="/hello">Say hi</a>\n', undefined, 200);
   };
   let server = new HTTPServer({ port: 1080 });
   server.addHandler("^/$", handler);
   server.addHandler("^/hello$", function() new HTTPResponse('Hello!<br><a href="/">Go back</a>'));
   server.run();
}

main();
