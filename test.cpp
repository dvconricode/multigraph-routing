#include "libcgr.cpp"
#include <vector>
#include <iostream>

using namespace cgr;

int main() {

	std::vector<Contact> contact_plan = cp_load("cgrTutorial.json");
	int dest_id = 5;
	
	//std::cout << "---contact plan---" << std::endl;
	//std::cout << contact_plan << std::endl;

	//ContactMultigraph cm(contact_plan, dest_id);*/

	//for (auto& vertex : cm.vertices) {
	//	std::cout << "Vertex: " << vertex.first << std::endl;
	//	std::cout << "Adjacencies: ";
	//	for (auto& adjacency : vertex.second.adjacencies) {
	//		std::cout << adjacency.first;
	//		std::cout << ", ";
	//	}
	//	std::cout << std::endl;
	//}

	/*for (Contact &c : contact_plan) {
		std::cout << &c << std::endl;
	}*/

	Contact rootContact = cgr::Contact(1, 1, 0, cgr::MAX_SIZE, 100, 1.0, 0);
	rootContact.arrival_time = 0;

	Route bestRoute = cmr_dijkstra(&rootContact, dest_id, contact_plan);


	std::vector<Contact> hops = bestRoute.get_hops();
	int expected_hops = 3;

	for (int i = 0; i < expected_hops; ++i) {
		std::cout << "Hop " << i << ": " << hops[i] << std::endl;
	}

	return 0;
}
