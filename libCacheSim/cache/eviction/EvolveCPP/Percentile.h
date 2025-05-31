#include <ext/pb_ds/assoc_container.hpp>
#include <ext/pb_ds/tree_policy.hpp>

template <typename T>
class OrderedMultiset : public __gnu_pbds::tree<
    T,
    __gnu_pbds::null_type,
    std::less_equal<T>,
    __gnu_pbds::rb_tree_tag,
    __gnu_pbds::tree_order_statistics_node_update> {

public:
    // Removes one instance of the given value (if present)
    bool remove(const T& value) {
        auto it = this->find(value);
        if (it != this->end()) {
            this->erase(it);
            return true;
        }
        return false;
    }

    // Returns the value at the p-th percentile (e.g. p=0.25 for 25%)
    T percentile(double p) const {
        if (this->empty()) {
            return 0;
        }
        p = std::clamp(p, 0.0, 1.0);
        size_t idx = static_cast<size_t>(p * this->size());
        if (idx >= this->size()) idx = this->size() - 1;
        return *this->find_by_order(idx);
    }
};

template <typename T>
class AgePercentileView {
    const OrderedMultiset<T>& start_timestamps;
    T current_time;

public:
    AgePercentileView(const OrderedMultiset<T>& timestamps, T now)
        : start_timestamps(timestamps), current_time(now) {}

    T percentile(double p) const {
        p = std::clamp(1.0 - p, 0.0, 1.0);
        T start = start_timestamps.percentile(p);
        return current_time - start;
    }
};