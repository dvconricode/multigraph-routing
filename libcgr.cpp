#include "libcgr.h"

//#include <boost/property_tree/ptree.hpp>
//#include <boost/property_tree/json_parser.hpp>

#include "boost/property_tree/ptree.hpp"
#include "boost/property_tree/json_parser.hpp"

#include <iostream>
#include <queue>

namespace cgr {

/*
 * Class method implementations.
 */
void Contact::clear_dijkstra_working_area() {
    arrival_time = MAX_SIZE;
    visited = false;
    predecessor = NULL;
    visited_nodes.clear();
}

bool Contact::operator==(const Contact contact) const {
    return (frm == contact.frm &&
            to == contact.to &&
            start == contact.start &&
            end == contact.end &&
            rate == contact.rate &&
            owlt == contact.owlt &&
            confidence == contact.confidence);
}

bool Contact::operator!=(const Contact contact) const {
    return !(*this == contact);
}

Contact::Contact(nodeId_t frm, nodeId_t to, int start, int end, int rate, float confidence, int owlt)
    : frm(frm), to(to), start(start), end(end), rate(rate), confidence(confidence), owlt(owlt)
{
    // fixed parameters
    volume = rate * (end - start);

    // variable parameters
    mav = std::vector<int>({volume, volume, volume});

    // route search working area
    arrival_time = MAX_SIZE;
    visited = false;
    visited_nodes = std::vector<nodeId_t>();
    predecessor = NULL;

    // route management working area
    suppressed = false;
    suppressed_next_hop = std::vector<Contact>();

    // forwarding working area
    // first_byte_tx_time = NULL;
    // last_byte_tx_time = NULL;
    // last_byte_arr_time = NULL;
    // effective_volume_limit = NULL;
}
    
Contact::Contact()
{
}

Contact::~Contact() {}

Route::Route()
{
}

Route::~Route() {}

Route::Route(Contact contact, Route *parent)
    : parent(parent)
{
    hops = std::vector<Contact>();
    if (NULL == parent) {
        // to_node = NULL;
        // next_node = NULL;
        from_time = 0;
        to_time = MAX_SIZE;
        best_delivery_time = 0;
        volume = MAX_SIZE;
        confidence = 1;
        __visited = std::map<nodeId_t, bool>();
    } else {
        to_node = parent->to_node;
        next_node = parent->next_node;
        from_time = parent->from_time;
        to_time = parent->to_time;
        best_delivery_time = parent->best_delivery_time;
        volume = parent->volume;
        confidence = parent->confidence;
        __visited = std::map<nodeId_t, bool>(parent->__visited); // copy
    }

    append(contact);
}

Contact Route::get_last_contact() {
    if (hops.empty()) {
        throw EmptyContainerError();
    } else {
        return hops.back();
    }
}

bool Route::visited(nodeId_t node) {
    //const int id = node;
    return __visited.count(node) && __visited[node];
}

void Route::append(Contact contact) {
    assert(eligible(contact));
    hops.push_back(contact);
    __visited[contact.frm] = true;
    __visited[contact.to] = true;

    refresh_metrics();
}

void Route::refresh_metrics() {
    assert(!hops.empty());
    std::vector<Contact> allHops = get_hops();
    to_node = allHops.back().to;
    next_node = allHops[0].to;
    from_time = allHops[0].start;
    to_time = MAX_SIZE;
    best_delivery_time = 0;
    confidence = 1;
    for (Contact contact : allHops) {
        to_time = std::min(to_time, contact.end);
        best_delivery_time = std::max(best_delivery_time + contact.owlt, contact.start + contact.owlt);
        confidence *= contact.confidence;
    }

    // volume
    int prev_last_byte_arr_time = 0;
    int min_effective_volume_limit = MAX_SIZE;
    for (Contact& contact : allHops) {
        if (contact == allHops[0]) {
            contact.first_byte_tx_time = contact.start;
        }
        else {
            contact.first_byte_tx_time = std::max(contact.start, prev_last_byte_arr_time);
        }
        int bundle_tx_time = 0;
        contact.last_byte_tx_time = contact.first_byte_tx_time + bundle_tx_time;
        contact.last_byte_arr_time = contact.last_byte_tx_time + contact.owlt;
        prev_last_byte_arr_time = contact.last_byte_arr_time;

        int effective_start_time = contact.first_byte_tx_time;
        int min_succ_stop_time = MAX_SIZE;
        std::vector<Contact>::iterator it = std::find(allHops.begin(), allHops.end(), contact);
        for (it; it < allHops.end(); ++it) {
            Contact successor = *it;
            if (successor.end < min_succ_stop_time) {
                min_succ_stop_time = successor.end;
            }
        }
        int effective_stop_time = std::min(contact.end, min_succ_stop_time);
        int effective_duration = effective_stop_time - effective_start_time;
        contact.effective_volume_limit = std::min(effective_duration * contact.rate, contact.volume);
        if (contact.effective_volume_limit < min_effective_volume_limit) {
            min_effective_volume_limit = contact.effective_volume_limit;
        }
    }
    volume = min_effective_volume_limit;
}

bool Route::eligible(Contact contact) {
    try {
        Contact last = get_last_contact();
        return (!visited(contact.to) && contact.end > last.start + last.owlt);
    }
    catch (EmptyContainerError) {
        return true;
    }
}

std::vector<Contact> Route::get_hops() {
    if (NULL == parent) {
        return hops;
    }
    else {
        std::vector<Contact> v(parent->get_hops());
        v.insert(v.end(), hops.begin(), hops.end());
        return v;
    }
}

Vertex::Vertex() {
    // this should never be used
}

Vertex::Vertex(nodeId_t node_id) {
    id = node_id;
    adjacencies = std::unordered_map<nodeId_t, std::vector<Contact>>();
    arrival_time = MAX_SIZE;
    visited = false;
    predecessor = NULL;
}

bool Vertex::operator<(const Vertex& v) const {
    return arrival_time < v.arrival_time;
}

bool CompareArrivals::operator()(const Vertex* v1, const Vertex* v2) {
    // smaller id breaks tie
    if (v1->arrival_time == v2->arrival_time) {
        return v1->id > v2->id;
    }
    return v1->arrival_time > v2->arrival_time;
}


ContactMultigraph::ContactMultigraph(std::vector<Contact> contact_plan, nodeId_t dest_id) {
    vertices = std::unordered_map<nodeId_t, Vertex*>();
    auto vertices_end = vertices.end();
    for (Contact& contact : contact_plan) {
        if (vertices.find(contact.frm) == vertices_end) { // if the frm vertex does not exist
            Vertex frm(contact.frm);
            // Put contact in `frm node`'s adjacency list to `to node`
            std::vector<Contact> adj = frm.adjacencies[contact.to];
            adj.push_back(contact);
            vertices.insert({ contact.frm, & frm });
        }
        else { // if the frm vertex does exist
            // Get list of contacts from `from node` to `to node`
            std::vector<Contact> adj = vertices[contact.frm]->adjacencies[contact.to];
            // If the contact list is empty or the latest contact in the list is
            // earlier than the current contact's start, put the current contact at the end of the list
            if (adj.empty() || contact.start > adj.back().start) {
                vertices[contact.frm]->adjacencies[contact.to].push_back(contact);
            }
            else { // if the current contact needs to be inserted somewhere inside the list
                // assuming non-overlapping contacts
                // find the index that the contact needs to be inserted at
                int index = cgr::contact_search_index(adj, contact.start);
                // insert contact sorted by start time
                vertices[contact.frm]->adjacencies[contact.to].insert(vertices[contact.frm]->adjacencies[contact.to].begin() + index, contact);
            }
        }
    }
    // Ensure the destination vertex exists since we're building vertices in the contact plan based on
    // the contact's `frm`. Any other node that is only `to` but never `frm` we can ignore and not construct
    // because it will never be part of the optimal path
    if (vertices.find(dest_id) == vertices_end) {
        Vertex dest(dest_id);
        vertices.insert({ dest_id, & dest });
    }
}


/*
 * Library function implementations, e.g. loading, routing algorithms, etc.
 */
std::vector<Contact> cp_load(std::string filename, int max_contacts) {
    std::vector<Contact> contactsVector;
    boost::property_tree::ptree pt;
    boost::property_tree::read_json(filename, pt);
    const boost::property_tree::ptree & contactsPt
        = pt.get_child("contacts", boost::property_tree::ptree());
    for (const boost::property_tree::ptree::value_type &eventPt : contactsPt) {
        Contact new_contact = Contact(eventPt.second.get<int>("source", 0),
                                      eventPt.second.get<int>("dest", 0),
                                      eventPt.second.get<int>("startTime", 0),
                                      eventPt.second.get<int>("endTime", 0),
                                      eventPt.second.get<int>("rate", 0));
        // new_contact.id = eventPt.second.get<int>("contact", 0);
        contactsVector.push_back(new_contact);
        if (contactsVector.size() == max_contacts) {
            break;
        }
    }
    return contactsVector;
}

Route dijkstra(Contact *root_contact, nodeId_t destination, std::vector<Contact> contact_plan) {
    // Need to clear the real contacts in the contact plan
    // so we loop using Contact& instead of Contact
    for (Contact &contact : contact_plan) {
        if (contact != *root_contact) {
            contact.clear_dijkstra_working_area();
        }
    }

    // Make sure we map to pointers so we can modify the underlying contact_plan
    // using the hashmap. The hashmap helps us find the neighbors of a node.
    std::map<nodeId_t, std::vector<Contact*>> contact_plan_hash;
    for (Contact &contact : contact_plan ) {
        if (!contact_plan_hash.count(contact.frm)) {
            contact_plan_hash[contact.frm] = std::vector<Contact*>();
        }
        if (!contact_plan_hash.count(contact.to)) {
            contact_plan_hash[contact.to] = std::vector<Contact*>();
        }
        contact_plan_hash[contact.frm].push_back(&contact);
    }

    Route route;
    Contact *final_contact = NULL;
    int earliest_fin_arr_t = MAX_SIZE;
    int arrvl_time;

    Contact *current = root_contact;

    if (!vector_contains(root_contact->visited_nodes, root_contact->to)) {
        root_contact->visited_nodes.push_back(root_contact->to);
    }

    while (true) {
        // loop over the neighbors of the current contact's source node
        for (Contact* contact : contact_plan_hash[current->to]) {
            if (vector_contains(current->suppressed_next_hop, *contact)) {
                continue;
            }
            if (contact->suppressed) {
                continue;
            }
            if (contact->visited) {
                continue;
            }
            if (vector_contains(current->visited_nodes, contact->to)) {
                continue;
            }
            if (contact->end <= current->arrival_time) {
                continue;
            }
            if (*std::max_element(contact->mav.begin(), contact->mav.end()) <= 0) {
                continue;
            }
            if (current->frm == contact->to && current->to == contact->frm) {
                continue;
            }

            // Calculate arrival time (cost)
            if (contact->start < current->arrival_time) {
                arrvl_time = current->arrival_time + contact->owlt;
            } else {
                arrvl_time = contact->start + contact->owlt;
            }

            if (arrvl_time <= contact->arrival_time) {
                contact->arrival_time = arrvl_time;
                contact->predecessor = current;
                contact->visited_nodes = current->visited_nodes;
                contact->visited_nodes.push_back(contact->to);
                
                if (contact->to == destination && contact->arrival_time < earliest_fin_arr_t) {
                    earliest_fin_arr_t = contact->arrival_time;
                    final_contact = &(*contact);
                }
            }
        }

        current->visited = true;

        // determine next best contact
        int earliest_arr_t = MAX_SIZE;
        Contact *next_contact = NULL;
        // @source DtnSim
        // "Warning: we need to point finalContact to
        // the real contact in contactPlan..."
        // This is why we loop with a Contact& rather than a Contact
        for (Contact &contact : contact_plan) {
            if (contact.suppressed || contact.visited) {
                continue;
            }
            if (contact.arrival_time > earliest_fin_arr_t) {
                continue;
            }
            if (contact.arrival_time < earliest_arr_t) {
                earliest_arr_t = contact.arrival_time;
                next_contact = &contact;
            }
        }

        if (NULL == next_contact) {
            break;
        }

        current = next_contact;
    }

    if (final_contact != NULL) {
        std::vector<Contact> hops;
        Contact contact;
        for (contact = *final_contact; contact != *root_contact; contact = *contact.predecessor) {
            hops.push_back(contact);
        }
        
        route = Route(hops.back());
        hops.pop_back();
        while (!hops.empty()) {
            route.append(hops.back());
            hops.pop_back();
        }
    }

    return route;


}

/*
 * Helper functions
 */
template <typename T>
bool vector_contains(std::vector<T> vec, T ele) {
    typename std::vector<T>::iterator it = std::find(vec.begin(), vec.end(), ele);
    return it != std::end(vec);
}

// Throw this exception in methods that would otherwise try to access an empty container
// E.g. calling std::vector::back() on an empty vector is undefined behavior.
// Following the approach of the Python version CGR library, we use this class for Route::eligible()
// as a substitue for Python's IndexError
const char* EmptyContainerError::what() const throw() {
    return "Tried to access element of an empty container";
}

std::ostream& operator<<(std::ostream &out, const std::vector<Contact> &obj) {
    out << "[";
    for (int i = 0; i < obj.size()-1; i++) {
        out << obj[i] << ", ";
    }
    out << obj[obj.size()-1] << "]";

    return out;
}

std::ostream& operator<<(std::ostream &out, const Contact &obj) {
    static const boost::format fmtTemplate("%d->%d(%d-%d,d%d)[mav%.0f%%]");
    boost::format fmt(fmtTemplate);

    int min_vol = *std::min_element(obj.mav.begin(), obj.mav.end());
    double volume = 100.0 * min_vol / obj.volume;
    fmt % obj.frm % obj.to % obj.start % obj.end % obj.owlt % volume;
    const std::string message(std::move(fmt.str()));

    out << message;
    return out;
}

std::ostream& operator<<(std::ostream &out, const Route &obj) {
    static const boost::format fmtTemplate("to:%d|via:%d(%03d,%03d)|bdt:%d|hops:%d|vol:%d|conf:%.1f|%s");
    boost::format fmt(fmtTemplate);

    std::vector<Contact> routeHops = static_cast<Route>(obj).get_hops();

    fmt % obj.to_node % obj.next_node % obj.from_time % obj.to_time % obj.best_delivery_time
        % routeHops.size() % obj.volume % obj.confidence % routeHops;
    const std::string message(std::move(fmt.str()));

    out << message;
    return out;
}



/*
 * Returns the contact C in sorted vector of Contacts `contacts` with the smallest end time
 * where C.end >= arrival_time && C.start <= arrival_time.
 * Assumes non-overlapping intervals.
 */
Contact contact_search(std::vector<Contact> &contacts, int arrival_time) {
    int index = contact_search_index(contacts, arrival_time);
    return contacts[index];
}

/* 
 * Returns the index of the contact C in sorted vector of Contacts `contacts` with the smallest end time
 * where C.end >= arrival_time && C.start <= arrival_time.
 * Assumes non-overlapping intervals.
 */
int contact_search_index(std::vector<Contact> &contacts, int arrival_time) {
    int left = 0;
    int right = contacts.size() - 1;
    if (contacts[left].end > arrival_time) {
        return left;
    }
    int mid;
    while (left < right - 1) {
        mid = (left + right) / 2;
        if (contacts[mid].end > arrival_time) {
            right = mid;
        }
        else {
            left = mid;
        }
    }
    return right;
}

/*
 * Multigraph routing route-finding algorithm. Finds the shortest (least amount of time) path
 * to transfer data throughout a network of nodes connected by temporary contacts.
 * root_contact is a contact from the source node to the source node, and it's start time is when data first arrives to the source node
 * destination is the nodeID_t of the destination node
 * contact_plan is a vector of contacts that is used to construct the contact multigraph
 */
Route cmr_dijkstra(Contact* root_contact, nodeId_t destination, std::vector<Contact> contact_plan) {
    // Construct Contact Multigraph from Contact Plan
    ContactMultigraph CM(contact_plan, destination);
    // Default construction for each vertex sets arrival time to infinity,
    // visited to false, predecessor to null.
    // The source vertex's arrival time needs to be set to the start time given by the root contact.
    CM.vertices[root_contact->frm]->arrival_time = root_contact->start;
    

    
    // Construct min PQ of vertices ordered by arrival time
    std::priority_queue<Vertex*, std::vector<Vertex*>, CompareArrivals> PQ;
    for (auto v : CM.vertices) {
        PQ.push(v.second);
    }
    Vertex* v_curr;
    Vertex* v_next;
    // The root vertex will be the top of the priority queue
    v_curr = PQ.top();
    PQ.pop();
    while (true) {
        // ------------- Multigraph Review Procedure start -------------
        for (auto adj : v_curr->adjacencies) {
            Vertex* u = CM.vertices[adj.first];
            if (u->visited) {
                continue;
            }
            // If the latest contact leaving v_curr is closed by the time data gets to v_curr,
            // there are no valid contacts.
            std::vector<Contact> v_curr_to_u = v_curr->adjacencies[u->id];
            if (v_curr_to_u.back().end < v_curr->arrival_time) {
                continue;
            }
            // find earliest usable contact from v_curr to u
            Contact best_contact = contact_search(v_curr_to_u, v_curr->arrival_time);
            // owlt_mgn is used in the CMR algorithm, but is not part of this implementation because it was not used in CGR
            // best_arr_time is the best time u can be reached by taking a contact from v_curr. if this is the fastest known route
            // then update u's arrival time and predecessor
            int best_arr_time = std::max(best_contact.start, v_curr->arrival_time) + best_contact.owlt;
            if (best_arr_time < u->arrival_time) {
                u->arrival_time = best_arr_time;
                // update u's priority in PQ using "lazy deletion"
                // Source: https://stackoverflow.com/questions/9209323/easiest-way-of-using-min-priority-queue-with-key-update-in-c
                PQ.push(u);
                u->predecessor = &best_contact;
            }
        }
        v_curr->visited = true;
        // ------------- Multigraph Review Procedure end -------------
        // get the next closest vertex to do MRP with
        v_next = PQ.top();
        PQ.pop();
        if (v_next->id == destination) {
            break;
        }
        else {
            v_curr = v_next;
        }
    }


    // test prints for the sake of debugging

  /*  std::cout << "--- Contact Plan ---" << std::endl;
    for (Contact &c : contact_plan) {
        std::cout << c << std::endl;
    }
    std::cout << "--- Predecessors ---" << std::endl;
    for (auto pr : CM.predecessors) {
        if (pr.first == 1) {
            continue;
        }
        std::cout << "Vertex " << pr.first << ": ";
        std::cout << contact_plan[pr.second] << std::endl;
    }*/


    // construct route from predecessors
    std::vector<Contact> hops;
    Contact* contact;
    for (contact = v_next->predecessor; contact->frm != contact->to; contact = CM.vertices[contact->frm]->predecessor) {
        hops.push_back(*contact);
        if (contact->frm == root_contact->frm) { // meaning if we've just inserted our first contact
            break;
        }
    }
    Route route;
    route = Route(hops.back());
    hops.pop_back();
    while (!hops.empty()) {
        route.append(hops.back());
        hops.pop_back();
    }
    return route;
}


} // namespace cgr
