
#include <iostream>
#include <fstream>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

using namespace std;

const int BLOCK_SIZE = 4096;
const char* DB_FILE = "data.db";
const int CACHE_SIZE = 8192;

struct Key {
    char index[65];
    int value;

    Key() {
        memset(index, 0, sizeof(index));
        value = 0;
    }

    Key(const char* idx, int v) {
        memset(index, 0, sizeof(index));
        strncpy(index, idx, 64);
        value = v;
    }

    bool operator<(const Key& other) const {
        int cmp = strcmp(index, other.index);
        if (cmp != 0) return cmp < 0;
        return value < other.value;
    }

    bool operator>(const Key& other) const {
        return other < *this;
    }

    bool operator<=(const Key& other) const {
        return !(*this > other);
    }

    bool operator>=(const Key& other) const {
        return !(*this < other);
    }

    bool operator==(const Key& other) const {
        return value == other.value && strcmp(index, other.index) == 0;
    }
};

struct Node {
    bool is_leaf;
    int count;
    int parent;
    int next;
    Key keys[50];
    int children[51];

    Node() {
        is_leaf = false;
        count = 0;
        parent = -1;
        next = -1;
        memset(children, -1, sizeof(children));
    }
};

struct Metadata {
    int root;
    int next_block;
    int free_list_head;
};

class BPlusTree {
private:
    fstream fs;
    Metadata meta;

    // Let's use a simpler cache for speed.
    struct SimpleCache {
        int idx;
        Node node;
        bool dirty;
        bool occupied;
    };
    SimpleCache* simple_cache;

    Node* get_node(int idx) {
        int h = idx % CACHE_SIZE;
        if (simple_cache[h].occupied && simple_cache[h].idx == idx) {
            return &simple_cache[h].node;
        }
        if (simple_cache[h].occupied) {
            if (simple_cache[h].dirty) {
                fs.seekp(sizeof(Metadata) + (long long)simple_cache[h].idx * BLOCK_SIZE);
                fs.write(reinterpret_cast<const char*>(&simple_cache[h].node), sizeof(Node));
            }
        }
        simple_cache[h].idx = idx;
        simple_cache[h].occupied = true;
        simple_cache[h].dirty = false;
        fs.seekg(sizeof(Metadata) + (long long)idx * BLOCK_SIZE);
        fs.read(reinterpret_cast<char*>(&simple_cache[h].node), sizeof(Node));
        return &simple_cache[h].node;
    }

    void mark_dirty(int idx) {
        int h = idx % CACHE_SIZE;
        if (simple_cache[h].occupied && simple_cache[h].idx == idx) {
            simple_cache[h].dirty = true;
        }
    }

    void write_node_to_cache(int idx, const Node& node) {
        int h = idx % CACHE_SIZE;
        if (simple_cache[h].occupied && simple_cache[h].idx != idx) {
            if (simple_cache[h].dirty) {
                fs.seekp(sizeof(Metadata) + (long long)simple_cache[h].idx * BLOCK_SIZE);
                fs.write(reinterpret_cast<const char*>(&simple_cache[h].node), sizeof(Node));
            }
        }
        simple_cache[h].idx = idx;
        simple_cache[h].node = node;
        simple_cache[h].occupied = true;
        simple_cache[h].dirty = true;
    }

    void read_meta() {
        fs.seekg(0);
        fs.read(reinterpret_cast<char*>(&meta), sizeof(Metadata));
    }

    void write_meta() {
        fs.seekp(0);
        fs.write(reinterpret_cast<const char*>(&meta), sizeof(Metadata));
    }

    int allocate_node() {
        int idx;
        if (meta.free_list_head != -1) {
            idx = meta.free_list_head;
            Node* node = get_node(idx);
            meta.free_list_head = node->next;
        } else {
            idx = meta.next_block++;
        }
        write_meta();
        return idx;
    }

public:
    BPlusTree() {
        simple_cache = new SimpleCache[CACHE_SIZE];
        for(int i=0; i<CACHE_SIZE; ++i) {
            simple_cache[i].occupied = false;
            simple_cache[i].dirty = false;
        }
        fs.open(DB_FILE, ios::in | ios::out | ios::binary);
        if (!fs) {
            fs.open(DB_FILE, ios::out | ios::binary);
            fs.close();
            fs.open(DB_FILE, ios::in | ios::out | ios::binary);
            meta.root = -1;
            meta.next_block = 0;
            meta.free_list_head = -1;
            write_meta();
        } else {
            read_meta();
        }
    }

    ~BPlusTree() {
        if (fs.is_open()) {
            for (int i = 0; i < CACHE_SIZE; i++) {
                if (simple_cache[i].occupied && simple_cache[i].dirty) {
                    fs.seekp(sizeof(Metadata) + (long long)simple_cache[i].idx * BLOCK_SIZE);
                    fs.write(reinterpret_cast<const char*>(&simple_cache[i].node), sizeof(Node));
                }
            }
            write_meta();
            fs.close();
        }
        delete[] simple_cache;
    }

    void insert(const Key& key) {
        if (meta.root == -1) {
            meta.root = allocate_node();
            Node root;
            root.is_leaf = true;
            root.count = 1;
            root.keys[0] = key;
            write_node_to_cache(meta.root, root);
            return;
        }

        int curr_idx = meta.root;
        while (true) {
            Node* curr = get_node(curr_idx);
            if (curr->is_leaf) break;
            int i = upper_bound(curr->keys, curr->keys + curr->count, key) - curr->keys;
            curr_idx = curr->children[i];
        }

        Node* curr = get_node(curr_idx);
        int pos = lower_bound(curr->keys, curr->keys + curr->count, key) - curr->keys;
        if (pos < curr->count && curr->keys[pos] == key) return;

        for (int i = curr->count - 1; i >= pos; i--) {
            curr->keys[i + 1] = curr->keys[i];
        }
        curr->keys[pos] = key;
        curr->count++;
        mark_dirty(curr_idx);

        if (curr->count == 50) {
            split_leaf(curr_idx);
        }
    }

    void split_leaf(int idx) {
        Node* node_ptr = get_node(idx);
        Node node = *node_ptr; // Copy to avoid issues when get_node is called again
        int new_idx = allocate_node();
        Node new_node;
        new_node.is_leaf = true;
        new_node.parent = node.parent;
        new_node.count = 25;
        for (int i = 0; i < 25; i++) {
            new_node.keys[i] = node.keys[i + 25];
        }
        node.count = 25;
        new_node.next = node.next;
        node.next = new_idx;

        write_node_to_cache(idx, node);
        write_node_to_cache(new_idx, new_node);

        insert_into_parent(idx, new_node.keys[0], new_idx);
    }

    void insert_into_parent(int left_idx, const Key& key, int right_idx) {
        int p_idx = get_node(left_idx)->parent;
        if (p_idx == -1) {
            int new_root_idx = allocate_node();
            Node new_root;
            new_root.is_leaf = false;
            new_root.count = 1;
            new_root.keys[0] = key;
            new_root.children[0] = left_idx;
            new_root.children[1] = right_idx;
            write_node_to_cache(new_root_idx, new_root);
            meta.root = new_root_idx;
            write_meta();

            get_node(left_idx)->parent = new_root_idx;
            mark_dirty(left_idx);
            get_node(right_idx)->parent = new_root_idx;
            mark_dirty(right_idx);
            return;
        }

        Node* p = get_node(p_idx);
        int pos = lower_bound(p->keys, p->keys + p->count, key) - p->keys;
        for (int i = p->count - 1; i >= pos; i--) {
            p->keys[i + 1] = p->keys[i];
            p->children[i + 2] = p->children[i + 1];
        }
        p->keys[pos] = key;
        p->children[pos + 1] = right_idx;
        p->count++;
        mark_dirty(p_idx);

        get_node(right_idx)->parent = p_idx;
        mark_dirty(right_idx);

        if (p->count == 50) {
            split_internal(p_idx);
        }
    }

    void split_internal(int idx) {
        Node* node_ptr = get_node(idx);
        Node node = *node_ptr;
        int new_idx = allocate_node();
        Node new_node;
        new_node.is_leaf = false;
        new_node.parent = node.parent;
        new_node.count = 24;
        Key mid_key = node.keys[25];
        for (int i = 0; i < 24; i++) {
            new_node.keys[i] = node.keys[i + 26];
            new_node.children[i] = node.children[i + 26];
        }
        new_node.children[24] = node.children[50];
        node.count = 25;

        write_node_to_cache(idx, node);
        write_node_to_cache(new_idx, new_node);

        for (int i = 0; i <= new_node.count; i++) {
            get_node(new_node.children[i])->parent = new_idx;
            mark_dirty(new_node.children[i]);
        }

        insert_into_parent(idx, mid_key, new_idx);
    }

    void find(const char* index_str) {
        if (meta.root == -1) {
            cout << "null" << endl;
            return;
        }

        int curr_idx = meta.root;
        while (true) {
            Node* curr = get_node(curr_idx);
            if (curr->is_leaf) break;
            int j = 0;
            while (j < curr->count && strcmp(index_str, curr->keys[j].index) > 0) j++;
            curr_idx = curr->children[j];
        }

        bool found = false;
        bool first = true;
        while (curr_idx != -1) {
            Node* curr = get_node(curr_idx);
            int i = lower_bound(curr->keys, curr->keys + curr->count, Key(index_str, -2147483648)) - curr->keys;
            for (; i < curr->count; i++) {
                int cmp = strcmp(index_str, curr->keys[i].index);
                if (cmp == 0) {
                    if (!first) cout << " ";
                    cout << curr->keys[i].value;
                    found = true;
                    first = false;
                } else if (cmp < 0) {
                    if (found) {
                        cout << endl;
                        return;
                    }
                    cout << "null" << endl;
                    return;
                }
            }
            curr_idx = curr->next;
        }
        if (found) cout << endl;
        else cout << "null" << endl;
    }

    void remove(const Key& key) {
        if (meta.root == -1) return;

        int curr_idx = meta.root;
        while (true) {
            Node* curr = get_node(curr_idx);
            if (curr->is_leaf) break;
            int i = upper_bound(curr->keys, curr->keys + curr->count, key) - curr->keys;
            curr_idx = curr->children[i];
        }

        Node* curr = get_node(curr_idx);
        int pos = lower_bound(curr->keys, curr->keys + curr->count, key) - curr->keys;
        if (pos == curr->count || !(curr->keys[pos] == key)) return;

        for (int i = pos; i < curr->count - 1; i++) {
            curr->keys[i] = curr->keys[i + 1];
        }
        curr->count--;
        mark_dirty(curr_idx);
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    int n;
    if (!(cin >> n)) return 0;

    BPlusTree tree;

    while (n--) {
        string cmd;
        cin >> cmd;
        if (cmd == "insert") {
            char index[66];
            int value;
            cin >> index >> value;
            tree.insert(Key(index, value));
        } else if (cmd == "delete") {
            char index[66];
            int value;
            cin >> index >> value;
            tree.remove(Key(index, value));
        } else if (cmd == "find") {
            char index[66];
            cin >> index;
            tree.find(index);
        }
    }

    return 0;
}
