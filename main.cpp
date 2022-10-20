#include <iostream>
#include <vector>
#include <chrono>
#include <unistd.h>
#include <fstream>

using namespace std;

constexpr int mask = 1454213;
const long pagesize = sysconf(_SC_PAGE_SIZE);
ofstream dev_null("/dev/null");

/**
 * Creates cycle of indices of array using only positions with specified stride.
 * Indices masked using mask variable to beat predictor.
 *
 * @param memory_begin Begin of allocated memory. Must be available length * stride elements
 * @param length Count of elements in final array with specified stride.
 * @param stride Stride between elements in array
 */
void create_cycle(int *const memory_begin, const int length, const int stride = 1) {
    auto indices = vector<int>();
    for (int j = 1; j < length; ++j) indices.push_back(j);
    random_shuffle(indices.begin(), indices.end());
    indices.push_back(0);

    int cur = 0;
    for (int j = 0; j < length; ++j) {
        *(memory_begin + cur * stride) = (indices[j] * stride) ^ mask;
        cur = indices[j];
    }
}

/**
 * Iterates through `length` elements of the array using references created in `create_cycle`
 */
[[gnu::noinline]] void traverse(const int length, const int *memory_begin, int &sum) {
    for (int cur = 0, iter = 0; iter < length; ++iter) {
        cur = *(memory_begin + cur) ^ mask;
        sum += cur;
    }
}

/**
 * Measure time of 16 cycles of summarizing values in array created in `create_cycle`
 */
[[gnu::noinline]] long measure_time(int active_positions, const int *memory_begin, int &sum) {
    auto begin = chrono::high_resolution_clock::now();
    traverse(16 * active_positions, memory_begin, sum);
    auto end = chrono::high_resolution_clock::now();
    return (end - begin).count();
}

/**
 * Measure times of traversing array in random order for each stride and length.
 * Calls `on_result` function for each measured time.
 */
template<typename OnResult>
void measure(const vector<int> &stride_sizes, const vector<int> &length_sizes, const OnResult &on_result) {
    int sum = 0;

    for (int i = 0; i < length_sizes.size(); ++i) {
        const auto length = length_sizes[i];
        for (int j = 0; j < stride_sizes.size(); ++j) {
            const auto gap = stride_sizes[j];

            const int active_positions = length / gap;
            if (active_positions <= 4) continue;

            int *const memory_begin = reinterpret_cast<int *>(aligned_alloc(pagesize, sizeof(int) * length));

            create_cycle(memory_begin, active_positions, gap);

            traverse(32 * active_positions, memory_begin, sum);

            const long time = measure_time(active_positions, memory_begin, sum);

            on_result(i, j, time / active_positions);

            free(memory_begin);
        }
    }

    dev_null << sum;
}

/**
 * Calculates average value for array of times. Largest 20% dropped as outliers.
 */
double average(vector<long> &values) {
    if (values.empty()) return std::numeric_limits<double>::quiet_NaN();

    sort(values.begin(), values.end());

    int begin = 0;
    int end = values.size() * 0.8;

    long sum = 0;
    for (int i = begin; i < end; i++) sum += values[i];

    return static_cast<double>(sum) / (end - begin);
}

/**
 * Pretty-prints results of measurements for human analysis.
 */
void print_table(const vector<int> &stride_sizes,
                 const vector<int> &length_sizes,
                 const vector<vector<double>> &av_result) {
    const size_t lengths = length_sizes.size();
    const size_t strides = stride_sizes.size();

    auto output = vector<vector<string>>(strides + 1, vector<string>(lengths + 1));

    for (int i = 0; i < stride_sizes.size(); ++i)
        output[i + 1][0] = to_string(stride_sizes[i] * sizeof(int));

    for (int i = 0; i < length_sizes.size(); ++i)
        output[0][i + 1] = to_string(length_sizes[i] * sizeof(int) / 1024) + "KB";

    for (int i = 0; i < strides; ++i) {
        for (int j = 0; j < lengths; ++j) {
            auto str = to_string(av_result[j][i]);
            auto dot_idx = str.find('.');
            if (dot_idx == -1) dot_idx = str.size() - 2;
            auto _str = str.substr(0, dot_idx + 2);
            output[i + 1][j + 1] = _str;
        }
    }

    size_t max_length = 0;
    for (auto &line: output)
        for (auto &cell: line)
            max_length = max(max_length, cell.size());

    for (auto &line: output)
        for (auto &cell: line)
            while (cell.size() < max_length) cell.push_back(' ');

    for (auto &line: output) {
        for (auto &cell: line)
            cout << cell << " | ";
        cout << endl;
    }
}

int find_cache_length(const size_t lengths, const vector<vector<double>> &av_result) {
    for (int i = 1; i < lengths; ++i)
        if (av_result[i][0] > av_result[i - 1][0] * 1.2)
            return i - 1;
    return -1;
}

int find_cache_line_length(const int cache_length_idx, const size_t strides, const vector<vector<double>> &av_result) {
    for (int i = 1; i < strides; ++i)
        if (av_result[cache_length_idx + 1][i] > av_result[cache_length_idx + 1][i - 1] * 1.1)
            return i;
    return -1;
}


int main() {
    cout << "Expected to finish in 20 seconds" << endl;

    /// List interesting strides and sizes.
    auto stride_sizes = vector<int>{1};
    while (stride_sizes.back() <= 128) stride_sizes.push_back(stride_sizes.back() * 2);

    auto length_sizes = vector<int>{256};
    while (length_sizes.back() <= 64 * 1024) length_sizes.push_back(length_sizes.back() * 2);

    const size_t lengths = length_sizes.size();
    const size_t strides = stride_sizes.size();

    /// Measure timings multiple times.
    auto results = vector<vector<vector<long>>>(lengths, vector<vector<long>>(strides));
    for (int i = 0; i < 100; ++i)
        measure(stride_sizes, length_sizes, [&](int i, int j, long time) { results[i][j].push_back(time); });

    /// Calculate average timing for each params.
    auto av_result = vector<vector<double>>(lengths, vector<double>(strides));
    for (int i = 0; i < lengths; ++i)
        for (int j = 0; j < strides; ++j)
            av_result[i][j] = average(results[i][j]);

    cout << "Results: stride \\ memory length" << endl;
    print_table(stride_sizes, length_sizes, av_result);

    auto cache_length_idx = find_cache_length(strides, av_result);
    if (cache_length_idx == -1)
        cout << "Cache length not found" << endl;
    else
        cout << "Cache length: " << length_sizes[cache_length_idx] * sizeof(int) / 1024 << "KB" << endl;

    auto cache_line_length_idx = find_cache_line_length(cache_length_idx, strides, av_result);
    if (cache_line_length_idx == -1)
        cout << "Cache line length not found" << endl;
    else
        cout << "Cache line length: " << stride_sizes[cache_line_length_idx] * sizeof(int) << endl;

    cout << "Cache associativity calculation not implemented" << endl;
}
