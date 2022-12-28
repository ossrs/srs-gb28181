
/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2021 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

'use strict';

// Async-await-promise based SRS RTC Signaling.
function SrsRtcSignalingAsync() {
    var self = {};

    // The schema is ws or wss, host is ip or ip:port, display is nickname
    // of user to join the room.
    self.connect = async function (schema, host, room, display) {
        var url = schema + '://' + host + '/sig/v1/rtc';
        self.ws = new WebSocket(url + '?room=' + room + '&display=' + display);

        self.ws.onmessage = function(event) {
            var r = JSON.parse(event.data);
            var promise = self._internals.msgs[r.tid];
            if (promise) {
                promise.resolve(r.msg);
                delete self._internals.msgs[r.tid];
            } else {
                self.onmessage(r.msg);
            }
        };

        return new Promise(function (resolve, reject) {
            self.ws.onopen = function (event) {
                resolve(event);
            };

            self.ws.onerror = function (event) {
                reject(event);
            };
        });
    };

    // The message is a json object.
    self.send = async function (message) {
        return new Promise(function (resolve, reject) {
            var r = {tid: Number(parseInt(new Date().getTime()*Math.random()*100)).toString(16).slice(0, 7), msg: message};
            self._internals.msgs[r.tid] = {resolve: resolve, reject: reject};
            self.ws.send(JSON.stringify(r));
        });
    };

    self.close = function () {
        self.ws && self.ws.close();
        self.ws = null;

        for (const tid in self._internals.msgs) {
            var promise = self._internals.msgs[tid];
            promise.reject('close');
        }
    };

    // The callback when got messages from signaling server.
    self.onmessage = function (msg) {
    };

    self._internals = {
        // Key is tid, value is object {resolve, reject, response}.
        msgs: {}
    };

    return self;
}

// Parse params in query string.
function SrsRtcSignalingParse(location) {
    let query = location.href.split('?')[1];
    query = query? '?' + query : null;

    let wsSchema = location.href.split('wss=')[1];
    wsSchema = wsSchema? wsSchema.split('&')[0] : (location.protocol === 'http:'? 'ws' : 'wss');

    let wsHost = location.href.split('wsh=')[1];
    wsHost = wsHost? wsHost.split('&')[0] : location.hostname;

    let wsPort = location.href.split('wsp=')[1];
    wsPort = wsPort? wsPort.split('&')[0] : location.host.split(':')[1];

    let host = location.href.split('host=')[1];
    host = host? host.split('&')[0] : location.hostname;

    let room = location.href.split('room=')[1];
    room = room? room.split('&')[0] : null;

    let display = location.href.split('display=')[1];
    display = display? display.split('&')[0] : Number(parseInt(new Date().getTime()*Math.random()*100)).toString(16).toString(16).slice(0, 7);

    let autostart = location.href.split('autostart=')[1];
    autostart = autostart && autostart.split('&')[0] === 'true';

    // Remove data in query.
    let rawQuery = query;
    if (query) {
        query = query.replace('wss=' + wsSchema, '');
        query = query.replace('wsh=' + wsHost, '');
        query = query.replace('wsp=' + wsPort, '');
        query = query.replace('host=' + host, '');
        if (room) {
            query = query.replace('room=' + room, '');
        }
        query = query.replace('display=' + display, '');
        query = query.replace('autostart=' + autostart, '');

        while (query.indexOf('&&') >= 0) {
            query = query.replace('&&', '&');
        }
        query = query.replace('?&', '?');
        if (query.lastIndexOf('?') === query.length - 1) {
            query = query.slice(0, query.length - 1);
        }
        if (query.lastIndexOf('&') === query.length - 1) {
            query = query.slice(0, query.length - 1);
        }
    }

    // Regenerate the host of websocket.
    wsHost = wsPort? wsHost.split(':')[0] + ':' + wsPort : wsHost;

    return {
        query: query, rawQuery: rawQuery, wsSchema: wsSchema, wsHost: wsHost, host: host,
        room: room, display: display, autostart: autostart,
    };
}
