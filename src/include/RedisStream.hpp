#include <array>
#include <vector>
#include <memory>

#define OUT_DEGREE 11

template <typename T>
struct TrieNode {
    std::string id;
    std::array<std::unique_ptr<TrieNode<T>>, OUT_DEGREE> child;
    
    TrieNode<T> *next;
    TrieNode<T> *prev;

    T payload;
};

template <typename T>
class Trie {
public:

    Trie(): root_{std::make_unique<TrieNode<T>>()} { }

    /// @brief Finds the node that corresponds to the given id. Performs a regular walk
    /// down through the tre following the child pointers.
    /// @param id Id of the node
    /// @return Pointer to the node or nullptr if not found.
    const TrieNode<T> *getNode(const std::string& id) const {
        const TrieNode<T> *current = root_.get();
        for (size_t i = 0; i < id.size(); i++) {
            int idx = (id[i] == '-') ? (OUT_DEGREE-1) : (id[i] - '0');
            if (!current->child[idx]) return nullptr;
            current = current->child[idx].get();
        }
        return current;
    }

    TrieNode<T> *createNode(const std::string& id) {
        TrieNode<T> *current = root_.get();
        for (size_t i = 0; i < id.size(); i++) {
            int idx = (id[i] == '-') ? (OUT_DEGREE-1) : (id[i] - '0');
            if (!current->child[idx]) 
                current->child[idx] = std::make_unique<TrieNode<T>>();
            current = current->child[idx].get();
        }
        current->id = id;
        return current;
    }

    const TrieNode<T> *getRoot() const { return root_.get(); }
private:
    std::unique_ptr<TrieNode<T>> root_;
};


using StreamPayload = std::vector<std::pair<std::string, std::string>>;

class RedisStream {
    using Node = TrieNode<StreamPayload>;
public:
    RedisStream(): size_{0}, latest_node_{nullptr}{
        root_ = const_cast<Node *>(trie_.getRoot());
        latest_node_ = root_;
    }

    std::string addEntry(const std::string& id, StreamPayload data) {

        // validate id;
        auto new_node = trie_.createNode(id);
        new_node->payload = std::move(data);

        size_++;
        latest_id_ = id;
        latest_node_->next = new_node;
        latest_node_ = new_node;

        return id;
    }

    /// @brief Performs a range scan
    /// @param first 
    /// @param last 
    /// @param n 
    /// @return 
    std::vector<StreamPayload> queryRange(std::string_view first, std::string_view last, size_t n = INT_MAX) const {

        if (size_ == 0) return {};

        bool left_exclusive, right_exclusive;
        const Node *left, *right, *current; // right should point to element past the range.
        if (first == "-") left = findLeftMost_(root_);
        else {
            if (first[0] == '(') {
                first.remove_prefix(1);
                left = getLowerBoundNode(first,true);
            }
            else left = getLowerBoundNode(first, false);
        }
        if (last == "+") right = latest_node_->next;
        else right = getLowerBoundNode(last, true);

        // TODO: make sure that left points to start entry and right points to past of last entry.
        // get left and right.

        // find result size.
        size_t size = 0;
        for (current = left; (current != right && size < n); current = current->next, size++)
        ;

        std::vector<StreamPayload> result;
        result.reserve(size);

        for (current = left; (size > 0); current = current->next, size--)
            result.push_back(current->payload);
        return result;
    }

    /// @brief  Returns the Node that has id greater than or equal to the given id.
    /// @param id Id of the key.
    /// @return TrieNode<T> * or nullptr if greater id not found.
    const Node *getLowerBoundNode(std::string_view id, bool exclusive) const {
        auto root = trie_.getRoot();
        auto node = dfsTraverse_(id);
        if (node && exclusive && (id == node->id)) node = node->next;
        return node;
    }

private:
    size_t size_;
    Trie<StreamPayload> trie_;
    std::string latest_id_;
    Node *latest_node_;
    Node *root_;

    const Node *findLeftMost_(const Node* current) const {
        if (!current) return nullptr;

        // walk down the tree and incline towards left.
        bool found = false;
        while (current) {
            for (int i = 0; i < OUT_DEGREE; i++){
                if (current->child[i]) {
                    current = current->child[i].get();
                    found = true;
                    break;
                }
            }
            if (found) return current;
        }
        return nullptr;
    }

    const Node *dfsTraverse_(std::string_view id) const {
        const Node *current = root_;
        std::vector<std::pair<const Node *,int>> st;

        for (int i = 0; i < id.size(); i++) {
            int idx = (id[i] == '-') ? OUT_DEGREE-1 : id[i] - '0';

            if (current->child[idx]) {
                st.push_back(std::make_pair(current, idx));
                current = current->child[idx].get();
            }
            else break;
        }

        // phase 2:
        while (!st.empty()) {
            auto [curr, idx] = st.back();
            st.pop_back();
            // find next node that is greater than idx.
            for (int j = idx+1; j < OUT_DEGREE-1; j++) {
                if (curr->child[j]) {
                    return findLeftMost_(curr->child[j].get());
                }
            }
        }
        return current;
    }

};