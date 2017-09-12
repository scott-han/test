#ifndef DATA_PROCESSORS_SYNAPSE_CIRCULAR_BUFFER_H
#define DATA_PROCESSORS_SYNAPSE_CIRCULAR_BUFFER_H

#ifndef NO_WHOLE_PROGRAM
namespace {
#endif
namespace data_processors { namespace synapse { namespace misc {

template <typename T, typename Unsigned, Unsigned Capacity, Unsigned Data_Begin_Offset>
class Circular_Buffer {

	static_assert(::std::is_unsigned<Unsigned>::value == true, "Unsigned must be unsigned integral");
	static_assert(misc::is_po2(Capacity), "Capacity power of 2");
	static_assert(Capacity < ::std::numeric_limits<Unsigned>::max(), "Capacity should be less than max of Unsigned");

	typename ::std::aligned_storage<(Capacity + Data_Begin_Offset) * sizeof(T)>::type Raw_Data;
	T * const Data{reinterpret_cast<T*>(&Raw_Data) + Data_Begin_Offset};

	Unsigned Begin_Index{0};
	Unsigned End_Index{0};

public:
	struct Space_Information {
		Unsigned Total_Size;
		Unsigned A_Size;
		Unsigned B_Size;
		T * A_Begin;
		T * B_Begin;
	};

	Space_Information Used() const {
		Space_Information Info;
		Info.Total_Size = End_Index - Begin_Index; 

		if (!Info.Total_Size)
			Info.A_Size = Info.B_Size = 0;
		else {
			auto const Bounded_Begin_Index(Begin_Index & Capacity - 1);
			auto const Bounded_End_Index(End_Index & Capacity - 1);
			assert(Begin_Index != End_Index);
			if (Bounded_Begin_Index < Bounded_End_Index) {
				Info.A_Size = Info.Total_Size;
				Info.B_Size = 0;
			} else {
				Info.A_Size = Capacity - Bounded_Begin_Index;;
				Info.B_Size = Info.Total_Size - Info.A_Size;
				Info.B_Begin = Data;
			}
			Info.A_Begin = Data + Bounded_Begin_Index;
			assert(Assert_Spaces_State(Info));
		}
		return Info;
	}

	Space_Information Free() const {
		Space_Information Info;
		Info.Total_Size = Capacity - (End_Index - Begin_Index); 
		if (!Info.Total_Size)
			Info.A_Size = Info.B_Size = 0;
		else {
			auto const Bounded_Begin_Index(Begin_Index & Capacity - 1);
			auto const Bounded_End_Index(End_Index & Capacity - 1);
			assert(!Bounded_Begin_Index || Begin_Index != End_Index);
			if (Bounded_Begin_Index > Bounded_End_Index) {
				Info.A_Size = Info.Total_Size;
				Info.B_Size = 0;
			} else {
				Info.A_Size = Capacity - Bounded_End_Index;
				Info.B_Size = Info.Total_Size - Info.A_Size;
				Info.B_Begin = Data;
			}
			Info.A_Begin = Data + Bounded_End_Index;
			assert(Assert_Spaces_State(Info));
		}
		return Info;
	}

	#ifndef NDEBUG
	bool Assert_Spaces_State(Space_Information const & Info) const {
		return (Info.A_Size || !Info.Total_Size)
			&& (Info.Total_Size <= Capacity)
			&& (Info.A_Begin >= Data) 
			&& (Info.A_Begin < Data + Capacity)
			&& (Info.A_Begin + Info.A_Size <= Data + Capacity)
			&& (!Info.B_Size || Info.B_Begin >= Data)
			&& (!Info.B_Size || Info.B_Begin < Data + Capacity)
			&& (!Info.B_Size || Info.B_Begin + Info.B_Size <= Data + Capacity)
			&& (Info.Total_Size == Info.A_Size + Info.B_Size)
		;
	}
	#endif

	void Consume(Unsigned const & Size) {
		assert(Free().Total_Size + Used().Total_Size == Capacity);
		assert(Size);
		assert(Used().Total_Size >= Size);
		assert(Used().Total_Size <= Capacity);
		if ((Begin_Index += Size) == End_Index)
			Begin_Index = End_Index = 0; // This one takes care of cases needing maximum continuous size; as well as simplifying the Free/Used calculations.
		assert(Free().Total_Size + Used().Total_Size == Capacity);
	}

	void Append(Unsigned const & Size) {
		assert(Free().Total_Size + Used().Total_Size == Capacity);
		assert(Size);
		assert(Used().Total_Size <= Capacity);
		assert(Capacity - Used().Total_Size >= Size);
		End_Index += Size;
		assert(End_Index != Begin_Index);
		assert(Free().Total_Size + Used().Total_Size == Capacity);
	}

};

}}}
#ifndef NO_WHOLE_PROGRAM
}
#endif

#endif
