import React, { useState, useCallback, useEffect } from 'react';
import useWebSocket, { ReadyState } from 'react-use-websocket';
import C3Chart from 'react-c3js';
import 'c3/c3.css';

export default function PowerHawk() {
    const [socketUrl, setSocketUrl] = useState('ws://10.1.10.223/powerhawk');
    const [data, setData] = useState({columns: [['data']]});

    const { lastMessage, readyState } = useWebSocket(socketUrl);

    useEffect(() => {
        if (lastMessage !== null) {
            var d = JSON.parse(lastMessage.data);
            // console.log(d);
            var samples = ['data'].concat(d.samples);
            setData({columns: [samples]});
        }
    }, [lastMessage]);

    const connectionStatus = {
        [ReadyState.CONNECTING]: 'Connecting',
        [ReadyState.OPEN]: 'Open',
        [ReadyState.CLOSING]: 'Closing',
        [ReadyState.CLOSED]: 'Closed',
        [ReadyState.UNINSTANTIATED]: 'Uninstantiated',
    }[readyState];

    return (
        <div>
            <span>The WebSocket is currently {connectionStatus}</span>
            <C3Chart data={data} />
        </div>
    );
};
