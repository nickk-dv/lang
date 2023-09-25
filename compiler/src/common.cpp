#include <stdio.h>
#include <stdlib.h>
#include <chrono>

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;
struct String;
struct StringView;
struct Timer;
struct Arena;
struct StringViewHasher;

constexpr u64 string_hash_ascii_9(const StringView& str);
constexpr u64 hash_ascii_9(const char* str);
bool os_file_read_all(const char* file_path, String* str);
u64 hash_fnv1a(const StringView& str);

struct String
{
	~String()
	{
		free(data);
	}

	u8* data;
	size_t count;
};

struct StringView
{
	StringView(const String& str)
	{
		data = str.data;
		count = str.count;
	}

	bool operator == (const StringView& other) const
	{
		if (count != other.count) return false;
		for (u64 i = 0; i < count; i++)
		if (data[i] != other.data[i]) return false;
		return true;
	}

	u8* data = NULL;
	size_t count = 0;
};

struct Timer
{
	typedef std::chrono::high_resolution_clock Clock;
	typedef std::chrono::steady_clock::time_point TimePoint;
	typedef std::chrono::nanoseconds Ns;

	void start()
	{
		t0 = Clock::now();
	}
	
	void end(const char* message)
	{
		TimePoint t1 = Clock::now();
		Ns ns = std::chrono::duration_cast<Ns>(t1 - t0);
		const float ns_to_ms = 1000000.0f;
		printf("%s ms: %f\n", message, ns.count() / ns_to_ms);
	}

	TimePoint t0;
};

struct Arena
{
	Arena() {};
	~Arena() { drop(); }

	template<typename T>
	T* alloc()
	{
		if (m_offset + sizeof(T) > m_block_size) alloc_block();
		T* ptr = (T*)(m_data + m_offset);
		m_offset += sizeof(T);
		return ptr;
	}
	
	void init(size_t block_size)
	{
		m_block_size = block_size;
		alloc_block();
	}

	void drop()
	{
		for (u8* block : m_data_blocks) free(block);
	}

	void alloc_block()
	{
		m_offset = 0;
		m_data = (u8*)malloc(m_block_size);
		if (m_data != NULL)
		memset(m_data, 0, m_block_size);
		m_data_blocks.emplace_back(m_data);
	}

	u8* m_data = NULL;
	size_t m_offset = 0;
	size_t m_block_size = 0;
	std::vector<u8*> m_data_blocks;
};

struct StringViewHasher
{
	size_t operator()(const StringView& str) const
	{
		return hash_fnv1a(str);
	}
};

constexpr u64 string_hash_ascii_9(const StringView& str)
{
	u64 hash = 0;
	for (u32 i = 0; i < str.count; i++)
		hash = (hash << 7) | (u64)str.data[i];
	return hash;
}

constexpr u64 hash_ascii_9(const char* str)
{
	u64 hash = 0;
	for (u32 i = 0; i < 9 && str[i] != '\0'; i++)
		hash = (hash << 7) | (u64)((u8)str[i]);
	return hash;
}

bool os_file_read_all(const char* file_path, String* str)
{
	FILE* file;
	fopen_s(&file, file_path, "rb");
	if (!file) return false;

	fseek(file, 0, SEEK_END);
	size_t file_size = (size_t)ftell(file);
	fseek(file, 0, SEEK_SET);

	if (file_size == 0)
	{
		fclose(file);
		return false;
	}

	void* buffer =  malloc(file_size);

	if (buffer == NULL)
	{
		fclose(file);
		return false;
	}
	
	size_t read_size = fread(buffer, 1, file_size, file);
	fclose(file);

	if (read_size != file_size)
	{
		free(buffer);
		return false;
	}
	
	str->data = (u8*)buffer;
	str->count = file_size;
	
	return true;
}

struct Atom
{
	u32 hash;
	char* name;
};

bool atom_match(Atom* a, Atom* b)
{
	if (a == b) return true;
	if (a->hash != b->hash) return false;
	return strcmp(a->name, b->name) == 0;
}

bool match_string_view(StringView a, StringView b)
{
	return a == b;
}

u64 hash_fnv1a(const StringView& str)
{
	#define FNV_PRIME 0x00000100000001B3UL
	#define FNV_OFFSET 0xcbf29ce484222325UL

	u64 hash = FNV_OFFSET;
	for (u32 i = 0; i < str.count; i++)
	{
		hash ^= str.data[i];
		hash *= FNV_PRIME;
	}
	return hash;
}

u32 hash_fnv1a_32(u32 count, u8* str)
{
	#define FNV_PRIME_32 0x01000193UL
	#define FNV_OFFSET_32 0x811c9dc5UL

	u32 hash = FNV_OFFSET_32;
	for (u32 i = 0; i < count; i++)
	{
		hash ^= str[i];
		hash *= FNV_PRIME_32;
	}
	return hash;
}

struct StringStorage
{
	void init() { buffer = (char*)malloc(1024 * 16); }
	void destroy() { free(buffer); }
	~StringStorage() { destroy(); }

	void start_str() { 
		str_start = cursor; 
	}

	char* end_str() {
		put_char('\0');
		return buffer + str_start;
	}

	void put_char(char c) {
		buffer[cursor] = c;
		cursor += 1;
	}

	char* buffer;
	u64 cursor = 0;
	u64 str_start = 0;
};

template<typename KeyType, typename ValueType, typename HashType, bool (*match_proc)(KeyType a, KeyType b)>
struct HashTable {

	struct Slot {
		KeyType key;
		ValueType value;
		HashType hash;
	};

	void init(u32 size) { alloc_table(size); }
	void destroy() { free(array); }
	~HashTable() { destroy(); }

	void alloc_table(u32 size) {
		table_size = size;
		slots_filled = 0;
		resize_threshold = table_size - table_size / 4;
		array = (Slot*)malloc(sizeof(Slot) * table_size);
		if (array != NULL) memset(array, 0, sizeof(Slot) * table_size);
	}

	void add(KeyType key, ValueType value, HashType hash) {
		u32 slot = hash % table_size;

		while (array[slot].hash != 0 && !match_proc(key, array[slot].key)) {
			slot = (slot + 1) % table_size;
		}

		if (array[slot].hash == 0) {
			array[slot] = Slot { key, value, hash };
			slots_filled += 1;
			if (slots_filled == resize_threshold) grow();
		}
	}

	bool contains(KeyType key, HashType hash) {
		u32 slot = hash % table_size;
		while (array[i].hash != 0) {
			if (match_proc(key, array[slot].key)) 
				return true;
			slot = (slot + 1) % table_size;
		}
		return false;
	}

	std::optional<ValueType> find(KeyType key, HashType hash) {
		u32 slot = hash % table_size;
		while (array[slot].hash != 0) {
			if (match_proc(key, array[slot].key)) 
				return array[slot].value;
			slot = (slot + 1) % table_size;
		}
		return {};
	}

	void grow() {
		u32 table_size_old = table_size;
		Slot* array_old = array;
		alloc_table(table_size * 2);

		for (u32 i = 0; i < table_size_old; i++) {
			Slot slot = array_old[i];
			if (slot.hash != 0) add(slot.key, slot.value, slot.hash);
		}
		free(array_old);
	}

	Slot* array;
	u32 table_size;
	u32 slots_filled;
	u32 resize_threshold;
};

template<typename KeyType, typename HashType, bool (*match_proc)(KeyType a, KeyType b)>
struct HashSet {

	struct Slot {
		KeyType key;
		HashType hash;
	};

	void init(u32 size) { alloc_table(size); }
	void destroy() { free(array); }
	~HashSet() { destroy(); }

	void alloc_table(u32 size) {
		table_size = size;
		slots_filled = 0;
		resize_threshold = table_size - table_size / 4;
		array = (Slot*)malloc(sizeof(Slot) * table_size);
		if (array != NULL) memset(array, 0, sizeof(Slot) * table_size);
	}

	void add(KeyType key, HashType hash) {
		u32 slot = hash % table_size;

		while (array[slot].hash != 0 && !match_proc(key, array[slot].key)) {
			slot = (slot + 1) % table_size;
		}

		if (array[slot].hash == 0) {
			array[slot] = Slot { key, hash };
			slots_filled += 1;
			if (slots_filled == resize_threshold) grow();
		}
	}

	bool contains(KeyType key, HashType hash) {
		u32 slot = hash % table_size;
		while (array[i].hash != 0) {
			if (match_proc(key, array[slot].key))
				return true;
			slot = (slot + 1) % table_size;
		}
		return false;
	}

	void grow() {
		u32 table_size_old = table_size;
		Slot* array_old = array;
		alloc_table(table_size * 2);

		for (u32 i = 0; i < table_size_old; i++) {
			Slot slot = array_old[i];
			if (slot.hash != 0) add(slot.key, slot.value, slot.hash);
		}
		free(array_old);
	}

	Slot* array;
	u32 table_size;
	u32 slots_filled;
	u32 resize_threshold;
};
