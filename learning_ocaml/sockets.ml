(* open Unix *)

let () =
    let server_socket = Unix.socket PF_INET SOCK_STREAM 0 in
    let server_address = Unix.ADDR_INET (Unix.inet_addr_loopback, 8080) in

    Unix.setsockopt server_socket SO_REUSEADDR true;

    Unix.bind server_socket server_address;

    Unix.listen server_socket 5;
    Printf.printf "Server is listening on port 8080...\n%!";

    let client_socket, client_addr = Unix.accept server_socket in
    Printf.printf "Client connected!\n%!";

    let buffer = Bytes.create 1024 in
    let bytes_read = Unix.recv client_socket buffer 0 1024 [] in
    let message = Bytes.sub_string buffer 0 bytes_read in
    Printf.printf "Received message: %s\n%!" message;

    let response = "Hello from OCaml! I received your message.\n" in
    let _bytes_sent = Unix.send client_socket (Bytes.of_string response) 0 (String.length response) [] in

    Unix.close client_socket;
    Unix.close server_socket;
    Printf.printf "Connection closed. Server shutting down.\n%!"