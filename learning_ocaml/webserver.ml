(*
    This doesn't build, need to figure out how to bind to dream package.  
    GPT says use dune.  Ugh.
*)

let () =
  (* Dream.run spins up the server on localhost:8080 by default *)
  Dream.run
  @@ Dream.logger          (* 1. Middleware: Logs every request to the console *)
  @@ Dream.router [        (* 2. Router: Matches URLs to handler functions *)

       (* Matches GET "/" *)
       Dream.get "/" (fun _request ->
         Dream.html "<h1>Welcome to my OCaml Server!</h1><p>Try visiting /hello/yourname</p>");

       (* Matches GET "/hello/:name" (Dynamic routing parameter) *)
       Dream.get "/hello/:name" (fun request ->
         let name = Dream.param request "name" in
         Dream.html (Printf.sprintf "<h1>Hello, %s!</h1>" name));
     ]