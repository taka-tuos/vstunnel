<?php
    $data = file_get_contents('php://input');
    echo $data;
	
    $socket = socket_create(AF_INET, SOCK_STREAM, SOL_TCP);
    socket_connect($socket, '127.0.0.1', 2011);
    socket_write($socket, 'w');
    socket_write($socket, sprintf('%08x',strlen($data)));
    socket_write($socket, $data);
    socket_close($socket);
?>
