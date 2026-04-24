
#include <iostream>
#include <fstream>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

using namespace std;

const int BLOCK_SIZE = 4096;
const char* DB_FILE = "data.db";

struct Key {
    char index[65];
    int value;

    Key() {
        memset(index, 0, sizeof(index));
        value = 0;
    }

    Key(const char* idx, int v) {
        int len = strlen(idx);
        if (len > 64) len = 64;
        memcpy(index, idx, len);
        index[len] = '\0';
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
    int next; // next leaf or first child
    Key keys[50];
    int children[51]; // child block indices or values (not used for leaf values here)

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

    void read_node(int idx, Node& node) {
        fs.seekg(sizeof(Metadata) + (long long)idx * BLOCK_SIZE);
        fs.read(reinterpret_cast<char*>(&node), sizeof(Node));
    }

    void write_node(int idx, const Node& node) {
        fs.seekp(sizeof(Metadata) + (long long)idx * BLOCK_SIZE);
        fs.write(reinterpret_cast<const char*>(&node), sizeof(Node));
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
            Node node;
            read_node(idx, node);
            meta.free_list_head = node.next;
        } else {
            idx = meta.next_block++;
        }
        write_meta();
        return idx;
    }

    void free_node(int idx) {
        Node node;
        read_node(idx, node);
        node.next = meta.free_list_head;
        write_node(idx, node);
        meta.free_list_head = idx;
        write_meta();
    }

public:
    BPlusTree() {
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
            write_meta();
            fs.close();
        }
    }

    void insert(const Key& key) {
        if (meta.root == -1) {
            meta.root = allocate_node();
            Node root;
            root.is_leaf = true;
            root.count = 1;
            root.keys[0] = key;
            write_node(meta.root, root);
            return;
        }

        int curr_idx = meta.root;
        Node curr;
        while (true) {
            read_node(curr_idx, curr);
            if (curr.is_leaf) break;
            int i = 0;
            while (i < curr.count && key >= curr.keys[i]) i++;
            curr_idx = curr.children[i];
        }

        // Check for duplicate
        for (int i = 0; i < curr.count; i++) {
            if (curr.keys[i] == key) return;
        }

        // Insert into leaf
        int i = curr.count - 1;
        while (i >= 0 && curr.keys[i] > key) {
            curr.keys[i + 1] = curr.keys[i];
            i--;
        }
        curr.keys[i + 1] = key;
        curr.count++;
        write_node(curr_idx, curr);

        if (curr.count == 50) {
            split_leaf(curr_idx, curr);
        }
    }

    void split_leaf(int idx, Node& node) {
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

        write_node(idx, node);
        write_node(new_idx, new_node);

        insert_into_parent(idx, new_node.keys[0], new_idx);
    }

    void insert_into_parent(int left_idx, const Key& key, int right_idx) {
        Node left;
        read_node(left_idx, left);
        if (left.parent == -1) {
            int new_root_idx = allocate_node();
            Node new_root;
            new_root.is_leaf = false;
            new_root.count = 1;
            new_root.keys[0] = key;
            new_root.children[0] = left_idx;
            new_root.children[1] = right_idx;
            write_node(new_root_idx, new_root);
            meta.root = new_root_idx;
            write_meta();

            left.parent = new_root_idx;
            write_node(left_idx, left);
            Node right;
            read_node(right_idx, right);
            right.parent = new_root_idx;
            write_node(right_idx, right);
            return;
        }

        int p_idx = left.parent;
        Node p;
        read_node(p_idx, p);

        int i = p.count - 1;
        while (i >= 0 && p.keys[i] > key) {
            p.keys[i + 1] = p.keys[i];
            p.children[i + 2] = p.children[i + 1];
            i--;
        }
        p.keys[i + 1] = key;
        p.children[i + 2] = right_idx;
        p.count++;
        write_node(p_idx, p);

        Node right;
        read_node(right_idx, right);
        right.parent = p_idx;
        write_node(right_idx, right);

        if (p.count == 50) {
            split_internal(p_idx, p);
        }
    }

    void split_internal(int idx, Node& node) {
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

        write_node(idx, node);
        write_node(new_idx, new_node);

        for (int i = 0; i <= new_node.count; i++) {
            Node child;
            read_node(new_node.children[i], child);
            child.parent = new_idx;
            write_node(new_node.children[i], child);
        }

        insert_into_parent(idx, mid_key, new_idx);
    }

    void find(const char* index_str) {
        if (meta.root == -1) {
            cout << "null" << endl;
            return;
        }

        int curr_idx = meta.root;
        Node curr;
        while (true) {
            read_node(curr_idx, curr);
            if (curr.is_leaf) break;
            int i = 0;
            while (i < curr.count && strcmp(index_str, curr.keys[i].index) >= 0) {
                // If index_str matches curr.keys[i].index, we might need to go left or right.
                // In B+ Tree, internal keys are separators.
                // If key < keys[i], go children[i].
                // If key >= keys[i], go children[i+1].
                // Wait, if we have multiple entries with same index, they might span multiple leaves.
                // We want the FIRST entry with index_str.
                i++;
            }
            // Actually, to find the first possible leaf, we should go to children[i] where i is the first index such that index_str <= keys[i].index
            // Wait, let's re-evaluate.
            int j = 0;
            while (j < curr.count && strcmp(index_str, curr.keys[j].index) > 0) j++;
            curr_idx = curr.children[j];
        }

        bool found = false;
        bool first = true;
        while (curr_idx != -1) {
            read_node(curr_idx, curr);
            for (int i = 0; i < curr.count; i++) {
                int cmp = strcmp(index_str, curr.keys[i].index);
                if (cmp == 0) {
                    if (!first) cout << " ";
                    cout << curr.keys[i].value;
                    found = true;
                    first = false;
                } else if (cmp < 0) {
                    if (found) {
                        cout << endl;
                        return;
                    }
                    // If we haven't found it yet and we passed it, it's not there.
                    cout << "null" << endl;
                    return;
                }
            }
            curr_idx = curr.next;
        }
        if (found) cout << endl;
        else cout << "null" << endl;
    }

    void remove(const Key& key) {
        if (meta.root == -1) return;

        int curr_idx = meta.root;
        Node curr;
        while (true) {
            read_node(curr_idx, curr);
            if (curr.is_leaf) break;
            int i = 0;
            while (i < curr.count && key >= curr.keys[i]) i++;
            curr_idx = curr.children[i];
        }

        int pos = -1;
        for (int i = 0; i < curr.count; i++) {
            if (curr.keys[i] == key) {
                pos = i;
                break;
            }
        }

        if (pos == -1) return;

        for (int i = pos; i < curr.count - 1; i++) {
            curr.keys[i] = curr.keys[i + 1];
        }
        curr.count--;
        write_node(curr_idx, curr);
        // For simplicity, we don't merge nodes on deletion in this version.
        // Given the constraints and typical CP problems, this might be enough.
        // If not, I'll implement merging.
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
