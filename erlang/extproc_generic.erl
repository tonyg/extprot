-module(extproc_generic).

-export([decode/1, continue_decode/2]).
-export([unsigned_to_signed/1, signed_to_unsigned/1]).
-export([encoded_length/1, encode/1]).

-export([test/0]).

decode(Bin) ->
    case decode(Bin, fun (V, Rest) -> {ok, V, Rest} end) of
	{ok, Value, Rest} ->
	    {ok, Value, Rest};
	F ->
	    {more, F}
    end.

decode(Bin, K) ->
    decode_vint(Bin, fun (V, Rest) -> decode_with_tag(V, Rest, K) end).

decode_vint(Bin, K) ->
    decode_vint(Bin, 0, 0, K).

decode_vint(<<1:1, N:7, Rest/binary>>, Shift, Acc, K) ->
    decode_vint(Rest, Shift + 7, Acc bor (N bsl Shift), K);
decode_vint(<<0:1, N:7, Rest/binary>>, Shift, Acc, K) ->
    K(Acc bor (N bsl Shift), Rest);
decode_vint(<<>>, Shift, Acc, K) ->
    fun (Bin) -> decode_vint(Bin, Shift, Acc, K) end.

decode_with_tag(TagAndType, Rest, K) when TagAndType band 1 == 1 ->
    decode_vint(Rest, fun (ByteLength, Rest1) -> decode_with_tag(TagAndType bsr 4,
								 TagAndType band 15,
								 ByteLength,
								 Rest1,
								 K) end);
decode_with_tag(TagAndType, Rest, K) ->
    decode_with_tag(TagAndType bsr 4,
		    TagAndType band 15,
		    none,
		    Rest,
		    K).

decode_with_tag(Tag, 0, none, Rest, K) ->
    decode_vint(Rest, fun (Value, Rest1) -> K({Tag, Value}, Rest1) end);
decode_with_tag(Tag, 2, none, <<V:8/little-unsigned, Rest/binary>>, K) ->
    K({Tag, {bits8, V}}, Rest);
decode_with_tag(Tag, 4, none, <<V:32/little-unsigned, Rest/binary>>, K) ->
    K({Tag, {bits32, V}}, Rest);
decode_with_tag(Tag, 6, none, <<V:64/little-unsigned, Rest/binary>>, K) ->
    K({Tag, {bits64_long, V}}, Rest);
decode_with_tag(Tag, 8, none, <<V:64/little-float, Rest/binary>>, K) ->
    K({Tag, {bits64_float, V}}, Rest);
decode_with_tag(Tag, 10, none, Rest, K) ->
    K({Tag, enum}, Rest);

decode_with_tag(Tag, 1, _ByteLength, Rest, K) ->
    decode_vint(Rest, fun (NElems, Rest1) ->
			      decode_n(Rest1, NElems, [],
				       fun (Elems, Rest2) ->
					       K({Tag, list_to_tuple(Elems)}, Rest2)
				       end)
		      end);
decode_with_tag(Tag, 3, ByteLength, Bin, K) ->
    <<Bytes:ByteLength/binary, Rest/binary>> = Bin,
    K({Tag, Bytes}, Rest);
decode_with_tag(Tag, 5, _ByteLength, Rest, K) ->
    decode_vint(Rest, fun (NElems, Rest1) ->
			      decode_n(Rest1, NElems, [],
				       fun (Elems, Rest2) ->
					       K({Tag, Elems}, Rest2)
				       end)
		      end);
decode_with_tag(Tag, 7, _ByteLength, Rest, K) ->
    decode_vint(Rest, fun (NPairs, Rest1) ->
			      decode_n(Rest1, NPairs * 2, [],
				       fun (Elems, Rest2) ->
					       K({Tag, {assoc, group_assoc(Elems)}}, Rest2)
				       end)
		      end).

decode_n(Bin, 0, Acc, K) ->
    K(lists:reverse(Acc), Bin);
decode_n(Bin, N, Acc, K) ->
    decode(Bin, fun (V, Rest) -> decode_n(Rest, N - 1, [V | Acc], K) end).

group_assoc([K, V | Rest]) ->
    [{K, V} | group_assoc(Rest)];
group_assoc([]) ->
    [].

continue_decode(Bin, K) ->
    K(Bin).

unsigned_to_signed(N) when N band 1 == 1 ->
    -(N bsr 1);
unsigned_to_signed(N) ->
    N bsr 1.

signed_to_unsigned(N) when N < 0 ->
    ((-N) bsl 1) bor 1;
signed_to_unsigned(N) ->
    N bsl 1.

vint_length(N) when N >= 128 ->
    vint_length(N bsr 7) + 1;
vint_length(_) ->
    1.

encoded_length({Tag, Body}) ->
    case body_length(Body) of
	{fixed, N} -> vint_length(Tag bsl 4) + N;
	{variable, N} -> vint_length(Tag bsl 4) + vint_length(N) + N
    end.

body_length(N) when is_number(N) ->
    {fixed, vint_length(N)};
body_length({bits8, _}) ->
    {fixed, 1};
body_length({bits32, _}) ->
    {fixed, 4};
body_length({bits64_long, _}) ->
    {fixed, 8};
body_length({bits64_float, _}) ->
    {fixed, 8};
body_length(enum) ->
    {fixed, 0};
body_length(Bin) when is_binary(Bin) ->
    {variable, size(Bin)};
body_length({assoc, Entries}) ->
    {variable, vint_length(length(Entries)) + sum_assoc(Entries)};
body_length(Entries) when is_list(Entries) ->
    {variable, vint_length(length(Entries)) + sum(Entries)};
body_length(Entries) when is_tuple(Entries) ->
    {variable, vint_length(size(Entries)) + sum(tuple_to_list(Entries))}.

sum_assoc([]) -> 0;
sum_assoc([{K, V} | Rest]) -> encoded_length(K) + encoded_length(V) + sum_assoc(Rest).

sum([]) -> 0;
sum([V | Rest]) -> encoded_length(V) + sum(Rest).

encode_vint(N) when N >= 128 ->
    [(N band 127) bor 128 | encode_vint(N bsr 7)];
encode_vint(N) ->
    [N].

encode({Tag, Body}) ->
    case encode_body(Body) of
	{none, WireType, BodyEncoded} ->
	    [(Tag bsl 4) bor WireType, BodyEncoded];
	{ByteLength, WireType, BodyEncoded} ->
	    [(Tag bsl 4) bor WireType, encode_vint(ByteLength), BodyEncoded]
    end.

encode_body(N) when is_number(N) -> {none, 0, encode_vint(N)}; 
encode_body({bits8, V}) -> {none, 2, <<V:8/little-unsigned>>};
encode_body({bits32, V}) -> {none, 4, <<V:32/little-unsigned>>};
encode_body({bits64_long, V}) -> {none, 6, <<V:64/little-unsigned>>};
encode_body({bits64_float, V}) -> {none, 8, <<V:64/little-float>>};
encode_body(enum) -> {none, 10, <<>>};
encode_body(LengthPrefixedByElimination) ->
    {WireType, BodyEncoded} = encode_body1(LengthPrefixedByElimination),
    {variable, L} = body_length(LengthPrefixedByElimination),
    {L, WireType, BodyEncoded}.

encode_body1(Bin) when is_binary(Bin) -> {3, Bin};
encode_body1({assoc, Entries}) -> {7, [encode_vint(length(Entries)),
				       lists:map(fun ({K,V}) -> [encode(K), encode(V)] end,
						 Entries)]};
encode_body1(Entries) when is_list(Entries) -> {5, [encode_vint(length(Entries)),
						    lists:map(fun encode/1, Entries)]};
encode_body1(Entries) when is_tuple(Entries) -> {1, [encode_vint(size(Entries)),
						     lists:map(fun encode/1,
							       tuple_to_list(Entries))]}.

test() ->
    ok = filelib:fold_files("../c",
			    ".*\.extprot$" %%%%%" emacs comment balancer
			    , false, fun (F, ok) -> test(F) end, ok).

test(Filename) ->
    io:format("~p... ", [Filename]),
    {ok, Bin} = file:read_file(Filename),
    {ok, D, <<>>} = decode(Bin),
    E = list_to_binary(encode(D)),
    if
	E =:= Bin ->
	    ok = io:format("passed~n");
	true ->
	    ok = io:format("~ndec ~p~nenc ~p~norg ~p~n~n", [D, E, Bin])
    end.
