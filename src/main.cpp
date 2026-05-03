#include <iostream>
#include "lookupbuffer.hpp"

using namespace std;

int main() {

	LookupBuffer<int, int> buff{};

	jthread([&]{
		buff.insert(1, 2);
	});
	
	jthread([&] {
		sleep(1);
		auto rb = buff.drop_select(1, 500ms);
		if (rb.has_value()) {
			cout << "val 1: " << rb.value() << endl;	
		}
	});

	auto rb = buff.drop_select(5, 500ms);
	cout << "val 5: " << rb.has_value() << endl;
}