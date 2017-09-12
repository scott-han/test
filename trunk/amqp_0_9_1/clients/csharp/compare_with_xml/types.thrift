namespace * data_processors

struct imaginary_bet_pool
{
	1 : optional i64 When,
	2 : optional string description,
	3 : required string xid,
	4 : optional string sid,
	5 : optional map<binary, double> combinations;
}

