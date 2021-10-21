<?php
/**
 * push.php - 一秒に一回 json データを chunked data として掃き出す
 */

function output_chunk($chunk)
{
//    echo sprintf("%08x\r\n", strlen($chunk));
    echo $chunk . "\r\n";
}

header("Content-type: taxt/plain");
header("Transfer-encoding: chunked");
ob_flush();
flush();

$socket = socket_create(AF_INET, SOCK_STREAM, SOL_TCP);
socket_connect($socket, '127.0.0.1', 3647);
socket_write($socket, 'r');

while ( !connection_aborted() ) {
    socket_recv($socket, $len, 8, MSG_WAITALL);
    socket_recv($socket, $json, hexdec($len), MSG_WAITALL);

    output_chunk(
        $json . str_repeat(' ', 512)
    );
    ob_flush();
    flush();
}
echo "0\r\n\r\n";
