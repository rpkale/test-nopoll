#!/usr/bin/env node

var WebSocketServer = require('websocket').server;
var http = require('http');

var debug_local = false;
if (process.env.NOPOLL_TEST_DEBUG)
    debug_local = true;

function debug_log(message) {
    if (debug_local)
        console.log(message);
}

if (process.argv.length < 3) {
    console.log("Need Port number as an argument.");
    process.exit(1);
}
var port = process.argv[2];

var server = http.createServer(function(request, response) {
    console.log('Received request for ' + request.url);
    response.writeHead(404);
    response.end();
});


server.listen(port, function() {
    console.log('Server is listening on port ' + port);
}).on('error', function() {
    console.log('Error: Failed to listen on port ' + port);
});

wsServer = new WebSocketServer({
    httpServer: server,
    autoAcceptConnections: false
});


wsServer.on('request', function(request) {

    var connection = request.accept(null, request.origin);
    debug_log('Connection accepted from ' + connection.remoteAddress);
    connection.on('message', function(message) {
        if (message.type === 'utf8') {
            debug_log('Received Message: ' + message.utf8Data);
            var res = message.utf8Data.split(' ');
            if (res.length === 3) {
                var requestSize = parseInt(res[1]);
                var requestCount = parseInt(res[2]);
                debug_log('Sending ' + requestSize + ' bytes ' + requestCount + ' times');
                for (var i = 0; i < requestCount; i++) {
                    var buf = new Buffer(requestSize);
                    connection.sendBytes(buf);
                }
            }
        }
        else if (message.type === 'binary') {
            debug_log('Received Unexpected message: Message type is binary');
        }
    });
    connection.on('close', function(reasonCode, description) {
        debug_log('Peer ' + connection.remoteAddress + ' disconnected.');
    });
});
