#ifndef HXMMXH_STRING_H
#define HXMMXH_STRING_H

#include <assert.h>
#include <string>
#include <atomic>
#include <iterator>
#include <utility>
#include <memory>
#include <initializer_list>

#ifdef FOOL_ENDIAN_BE
constexpr auto kIsLittleEndian = false;
#else
constexpr auto kIsLittleEndian = true;
#endif

namespace fool
{
    namespace hstring_detail
    {
        // POD（Plain Old Data）指的是能够像C语言中的结构体那样进行处理的一种数据类型，比如能够使用memcpy()来复制内存，使用memset()进行初始化等。
        // 传入两个迭代器，从b开始拷贝n个元素到d中
        template <class InIt, class OutIt>
        inline std::pair<InIt, OutIt> copy_n(InIt b, typename std::iterator_traits<InIt>::difference_type n, OutIt d);
        // 把b和e之间的元素都设为c,
        template <class Pod, class T>
        inline void podFill(Pod *b, Pod *e, T c);
        // 把b和e之间的元素拷贝到d开头的空间
        template <class Pod>
        inline void podCopy(const Pod *b, const Pod *e, Pod *d);
        // 把b和e之间的元素移动到d开头的空间
        template <class Pod>
        inline void podMove(const Pod *b, const Pod *e, Pod *d);
    }
    // 定义一个特殊的获取方法来构造fbstring对象。AcquireMallocatedString意味着用户将一个指针传递给一个malloc分配的字符串，fbstring对象将保管这个字符串。??
    enum class AcquireMallocatedString
    {
    };
    class hstring_core
    {
    public:
        // 构造函数
        // 默认构造函数
        hstring_core() noexcept { reset(); }
        // 拷贝构造函数
        hstring_core(const hstring_core &rhs);
        // 移动构造函数
        hstring_core(hstring_core &&goner) noexcept;
        // 通过char*构造字符串
        hstring_core(const char *const data, const size_t size);
        // 接管一个已分配空间的字符串, size表示原string的大小，allocatedSize表示原来分配的空间的大小,allocatedSize >= size + 1 and data[size] == '\0.类型固定为中字符串
        hstring_core(char *const data, const size_t size, const size_t allocatedSize, AcquireMallocatedString);
        // 析构函数
        ~hstring_core() noexcept;

        // 禁止拷贝赋值
        hstring_core &operator=(const hstring_core &rhs) = delete;

        void swap(hstring_core &rhs);
        const char *data() const;
        char *data();
        // 返回可以修改的字符串，主要是针对COW的大字符串
        char *mutableData();
        const char *c_str() const;
        char *c_str();
        // 收缩空间，减小size,size变为size-delta
        void shrink(const size_t delta);
        // 设置容量，提高capacity，如果传入的值小于现有的capacity，不会缩小容量
        void reserve(size_t minCapacity);
        // 往字符串中增加delta个字符，exGrowth表示需要扩容时是否要分配额外的空间。返回新增加的字符的首地址
        char *expandNoinit(const size_t delta, bool expGrowth = false);
        void push_back(char c);
        // hstring的类型定义
        typedef uint8_t category_type;
        enum class Category : category_type
        {
            isSmall = 0,
            isMedium = kIsLittleEndian ? 0x80 : 0x2,
            isLarge = kIsLittleEndian ? 0x40 : 0x1,
        };
        // 获取字符串的类型
        Category category() const
        {
            // 大端和小端都是一样的方法，只是掩码不同
            // 小端是11000000，大端是00000011
            return static_cast<Category>(bytes_[lastChar] & categoryExtractMask);
        }
        // 获取字符串的大小
        size_t size() const;
        // 获取字符串的容量
        size_t capacity() const;
        // 是否在共享内存
        bool isShared() const
        {
            return category() == Category::isLarge && RefCounted::refs(ml_.data_) > 1;
        }

    private:
        class MediumLarge
        {
        public:
            // 获取容量
            size_t capacity() const;
            // 设置容量
            void setCapacity(size_t cap, Category cat);

        public:
            char *data_;
            size_t size_;
            size_t capacity_;
        };
        // 引用计数，用于ROW
        class RefCounted
        {
        public:
            // 获取data的偏移量
            static constexpr size_t getDataOffset();
            // 传入data的地址，得到RefCounted的地址
            static RefCounted *fromData(char *p);
            // 获取引用计数
            static size_t refs(char *p);
            // 递增引用计数
            static void incrementRefs(char *p);
            // 递减引用计数，要注意在引用计数位0时，析构对象
            static void decrementRefs(char *p);
            // 创建一个引用计数
            static RefCounted *create(size_t *size);
            static RefCounted *create(const char *data, size_t *size);
            // 重新分配一个容量更大的空间，调用这个函数必须保证没有人共享这个字符串
            static RefCounted *reallocate(
                char *const data,
                const size_t currentSize,
                const size_t currentCapacity,
                size_t *newCapacity);

        public:
            std::atomic<size_t> refCount_;
            char data_[1];
        };
        // 这个union的大小是MediumLarge的大小
        union
        {
            uint8_t bytes_[sizeof(MediumLarge)]; //一个字符数组，用来获取这个union最高地址的字节
            char small_[sizeof(MediumLarge) / sizeof(char)];
            MediumLarge ml_;
        };
        constexpr static size_t lastChar = sizeof(MediumLarge) - 1;
        // SSO的最大长度
        constexpr static size_t maxSmallSize = lastChar / sizeof(char);
        // medium string的最大长度
        constexpr static size_t maxMediumSize = 254 / sizeof(char);
        // 获取类型的掩码
        constexpr static uint8_t categoryExtractMask = kIsLittleEndian ? 0xC0 : 0x3;
        // 为了把category左移到最高位的那一个字节
        constexpr static size_t kCategoryShift = (sizeof(size_t) - 1) * 8;
        // 获取容量的掩码，小端法时用的上，把类型掩码左移到最高位然后取反
        constexpr static size_t capacityExtractMask = kIsLittleEndian
                                                          ? ~(size_t(categoryExtractMask) << kCategoryShift)
                                                          : 0x0 /* unused */;

        void copySmall(const hstring_core &);
        void copyMedium(const hstring_core &);
        void copyLarge(const hstring_core &);

        void initSmall(const char *data, size_t size);
        void initMedium(const char *data, size_t size);
        void initLarge(const char *data, size_t size);

        void reserveSmall(size_t minCapacity);
        void reserveMedium(size_t minCapacity);
        void reserveLarge(size_t minCapacity);

        void shrinkSmall(size_t delta);
        void shrinkMedium(size_t delta);
        void shrinkLarge(size_t delta);

        // 对于引用计数共享的字符串，脱离这个共享状态
        void unshare(size_t minCapacity = 0);
        // 对于大字符串，返回可修改的地址，即写时复制时用得上
        char *mutableDataLarge();

        // 获取小字符串的size
        size_t smallSize() const;
        // 设置小字符串的size
        void setSmallSize(size_t s);
        void reset() { setSmallSize(0); }
        void destroyMediumLarge() noexcept;
    };

    class basic_hstring
    {
        static constexpr size_t npos = size_t(-1);

    public:
        // 官方文档说的是，下面这两个构造函数实际上可以合成为 explicit basic_fbstring(const A& a = A()) noexcept { }
        // 但那样做的话，在用Clang编译时会报错chosen constructor is explicit in copy-initialization ... in implicit initialization of field '(x)' with omitted initializer"
        // 这两个构造函数也没干什么呀。。。
        basic_hstring() noexcept : basic_hstring(std::allocator<char>()) {}
        explicit basic_hstring(const std::allocator<char> &) noexcept {}
        // 拷贝构造函数和移动构造函数
        basic_hstring(const basic_hstring &str) : store_(str.store_) {}
        basic_hstring(basic_hstring &&goner) noexcept : store_(std::move(goner.store_)) {}
        // 和标准库的basic_string兼容
        basic_hstring(const std::basic_string<char> &str) : store_(str.data(), str.size()) {}

        basic_hstring(const basic_hstring &str, size_t pos, size_t n = npos, const std::allocator<char> & = std::allocator<char>())
        {
            assign(str, pos, n);
        }
        basic_hstring(const char *s, const std::allocator<char> & = std::allocator<char>()) : store_(s, strlen(s)) {}
        basic_hstring(const char *s, size_t n, const std::allocator<char> & = std::allocator<char>()) : store_(s, n) {}
        // n个c的字符串
        basic_hstring(size_t n, char c, const std::allocator<char> & = std::allocator<char>())
        {
            auto const pData = store_.expandNoinit(n);
            hstring_detail::podFill(pData, pData + n, c);
        }
        // 从迭代器中构造，保证迭代器中保存的元素能转换成char
        template <class InIt>
        basic_hstring(InIt begin, InIt end)
        {
            assign(begin, end);
        }

        basic_hstring(const char *b, const char *e) : store_(b, size_t(e - b)) {}

        // 接管空间，而不是重新分配空间
        basic_hstring(char *s, size_t n, size_t c, AcquireMallocatedString a) : store_(s, n, c, a) {}

        basic_hstring(std::initializer_list<char> il)
        {
            assign(il.begin(), il.end());
        }

        ~basic_hstring() noexcept {}

        basic_hstring &operator=(const basic_hstring &lhs);
        basic_hstring &operator=(basic_hstring &&goner) noexcept;

        basic_hstring &operator=(const std::basic_string<char> &rhs)
        {
            return assign(rhs.data(), rhs.size());
        }

        std::basic_string<char> toStdString() const
        {
            return std::basic_string<char>(data(), size());
        }

        basic_hstring &operator=(const char *s) { return assign(s); }

        basic_hstring &operator=(char c);
        basic_hstring &operator=(std::initializer_list<char> il)
        {
            return assign(il.begin(), il.end());
        }
        /*------------------------------------迭代器操作-----------------------------------------------------------------*/
        typedef char *iterator;
        typedef const char *const_iterator;
        typedef std::reverse_iterator<iterator> reverse_iterator;
        typedef std::reverse_iterator<const_iterator> const_reverse_iterator;
        // 注意分两种，不会修改的迭代器返回data(),有可能修改的迭代器返回mutableData()
        iterator begin() { return store_.mutableData(); }
        const_iterator begin() const { return store_.data(); }
        const_iterator cbegin() const { return begin(); }
        iterator end() { return store_.mutableData() + store_.size(); }
        const_iterator end() const { return store_.data() + store_.size(); }
        const_iterator cend() const { return end(); }
        reverse_iterator rbegin() { return reverse_iterator(end()); }
        const_reverse_iterator rbegin() const
        {
            return const_reverse_iterator(end());
        }
        const_reverse_iterator crbegin() const { return rbegin(); }
        reverse_iterator rend() { return reverse_iterator(begin()); }
        const_reverse_iterator rend() const
        {
            return const_reverse_iterator(begin());
        }
        const_reverse_iterator crend() const { return rend(); }
        /*------------------------------------元素访问操作-----------------------------------------------------------------*/
        const char &front() const { return *begin(); }
        const char &back() const
        {
            assert(!empty());
            return *(end() - 1);
        }
        char &front() { return *begin(); }
        char &back()
        {
            assert(!empty());
            return *(end() - 1);
        }
        const_reference operator[](size_type pos) const { return *(begin() + pos); }

        reference operator[](size_type pos) { return *(begin() + pos); }

        const_reference at(size_type n) const
        {
            enforce<std::out_of_range>(n < size(), "");
            return (*this)[n];
        }

        reference at(size_type n)
        {
            enforce<std::out_of_range>(n < size(), "");
            return (*this)[n];
        }
        void pop_back()
        {
            assert(!empty());
            store_.shrink(1);
        }
        /*------------------------------------大小，容量操作-----------------------------------------------------------------*/
        size_t size() const { return store_.size(); }
        size_t length() const { return size(); }
        size_t max_size() const { return std::numeric_limits<size_t>::max(); }
        void resize(size_t n, char c = char());
        size_t capacity() const { return store_.capacity(); }
        void reserve(size_t res_arg = 0)
        {
            store_.reserve(res_arg);
        }
        // 去除空闲的空间，让size==capacity,只有容量大于size的1.5倍才会执行
        void shrink_to_fit()
        {
            if (capacity() < size() * 3 / 2)
            {
                return;
            }
            basic_hstring(cbegin(), cend()).swap(*this);
        }
        void clear() { resize(0); }
        bool empty() const { return size() == 0; }

    private:
        hstring_core store_;
    };

}
#endif
