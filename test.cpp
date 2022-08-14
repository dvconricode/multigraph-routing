#include "libcgr.cpp"
#include <vector>
#include <iostream>

using namespace cgr;

int main() {
	std::vector<Contact> contact_plan = cp_load("cgrTutorial.json");
	
	std::cout << "---contact plan---" << std::endl;
	std::cout << contact_plan << std::endl;

	int dest_id = 5;
	ContactMultigraph cm(contact_plan, dest_id);

	for (auto& vertex : cm.vertices) {
		std::cout << "Vertex: " << vertex.first << std::endl;
		std::cout << "Adjacencies: " <<
		for (auto& adjacency : vertex.second.adjacencies) {
			std::cout << adjacency.first;
		}
		std::cout << std::endl;
	}

	return 0;
}
