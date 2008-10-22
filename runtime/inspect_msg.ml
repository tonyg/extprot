open Codec
module PP = Pretty_print

let read_byte = IO.read_byte

let read_vint t =
  let b = ref (read_byte t) in
  let x = ref 0 in
  let e = ref 0 in
    while !b >= 128 do
      x := !x + ((!b - 128) lsl !e);
      e := !e + 7;
      b := read_byte t
    done;
    !x + (!b lsl !e)

let read_prefix = read_vint

let (+!) = Int32.add
let (<!) = Int32.shift_left
let to_i32 = Int32.of_int

let (+!!) = Int64.add
let (<!!) = Int64.shift_left
let to_i64 = Int64.of_int

let zigzag_dec n = (n lsr 1) lxor (- (n land 1))

let rec inspect pp io prefix = match ll_type prefix with
    Tuple -> inspect_tuple "{" "}" prefix pp io
  | Htuple -> inspect_tuple "[" "]" prefix pp io
  | Vint -> PP.fprintf pp "Vint_t%d@[<1>@ %a@]" (ll_tag prefix) PP.pp_int (zigzag_dec (read_vint io))
  | Bits32 ->
      let a = read_byte io in
      let b = read_byte io in
      let c = read_byte io in
      let d = read_byte io in
      let i32 = to_i32 a +! (to_i32 b <! 8) +! (to_i32 c <! 16) +! (to_i32 d <! 24) in
        PP.fprintf pp "Bits32_t%d@[<1>@ %a@]" (ll_tag prefix) PP.pp_int32 i32
  | Bits64 ->
      let a = read_byte io in
      let b = read_byte io in
      let c = read_byte io in
      let d = read_byte io in
      let e = read_byte io in
      let f = read_byte io in
      let g = read_byte io in
      let h = read_byte io in
      let i64 = to_i64 a          +!! (to_i64 b <!! 8) +!!
                (to_i64 c <!! 16) +!! (to_i64 d <!! 24) +!!
                (to_i64 e <!! 32) +!! (to_i64 f <!! 40) +!!
                (to_i64 g <!! 48) +!! (to_i64 h <!! 56)
      in PP.fprintf pp "Bits64_t%d@[<1>@ %a@]" (ll_tag prefix) PP.pp_int64 i64
  | Bytes ->
      let len = read_vint io in
      let s = IO.nread io len in
        PP.fprintf pp "@[<1>Bytes_t%d@ %a@]" (ll_tag prefix) PP.pp_string s


and inspect_tuple left right prefix pp io =
  let tag = ll_tag prefix in
  let _ = read_vint io in (* len *)
  let nelms = read_vint io in
    PP.fprintf pp "@[<1>T%d %s@[<1>@ %a@]%s@]" tag left (inspect_elms io) nelms right

and inspect_elms io pp = function
    0 -> ()
  | 1 -> inspect pp io (read_prefix io)
  | n -> inspect pp io (read_prefix io);
         PP.fprintf pp ";@ ";
         inspect_elms io pp (n - 1)

let inspect pp io = inspect pp io (read_prefix io)
