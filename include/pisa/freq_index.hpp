#pragma once

#include "gsl/gsl_assert"
#include "gsl/span"
#include "tbb/parallel_invoke.h"

#include "bitvector_collection.hpp"
#include "codec/compact_elias_fano.hpp"
#include "codec/integer_codes.hpp"
#include "cursor.hpp"
#include "global_parameters.hpp"

namespace pisa {

    template <typename DocsSequence, typename FreqsSequence>
    class freq_index {
    public:
        freq_index()
            : m_num_docs(0)
        {}

        class builder {
        public:
            builder(uint64_t num_docs, global_parameters const& params)
                : m_params(params)
                , m_num_docs(num_docs)
                , m_docs_sequences(params)
                , m_freqs_sequences(params)
            {}

            template <typename DocsIterator, typename FreqsIterator>
            void add_posting_list(uint64_t n, DocsIterator docs_begin,
                                  FreqsIterator freqs_begin, uint64_t occurrences)
            {
                if (!n) throw std::invalid_argument("List must be nonempty");

                tbb::parallel_invoke(
                    [&] {
                        bit_vector_builder docs_bits;
                        write_gamma_nonzero(docs_bits, occurrences);
                        if (occurrences > 1) {
                            docs_bits.append_bits(n, ceil_log2(occurrences + 1));
                        }
                        DocsSequence::write(docs_bits, docs_begin, m_num_docs, n, m_params);
                        m_docs_sequences.append(docs_bits);
                    },
                    [&] {
                        bit_vector_builder freqs_bits;
                        FreqsSequence::write(freqs_bits, freqs_begin, occurrences + 1, n, m_params);
                        m_freqs_sequences.append(freqs_bits);
                    });
            }

            void build(freq_index& sq)
            {
                sq.m_num_docs = m_num_docs;
                sq.m_params = m_params;

                m_docs_sequences.build(sq.m_docs_sequences);
                m_freqs_sequences.build(sq.m_freqs_sequences);
            }

        private:
            global_parameters m_params;
            uint64_t m_num_docs;
            bitvector_collection::builder m_docs_sequences;
            bitvector_collection::builder m_freqs_sequences;
        };

        uint64_t size() const
        {
            return m_docs_sequences.size();
        }

        uint64_t num_docs() const
        {
            return m_num_docs;
        }

        class Cursor {
           public:
            using document_type = uint32_t;

            void reset()
            {
                m_cur_pos = 0;
                m_cur_docid = m_docs_enum.move(0).second;
            }

            void DS2I_FLATTEN_FUNC next()
            {
                auto val = m_docs_enum.next();
                m_cur_pos = val.first;
                m_cur_docid = val.second;
                if (m_cur_docid >= m_last) {
                    m_cur_docid = cursor::document_bound;
                }
            }

            void DS2I_FLATTEN_FUNC next_geq(uint64_t lower_bound)
            {
                auto val = m_docs_enum.next_geq(lower_bound);
                m_cur_pos = val.first;
                m_cur_docid = val.second;
                if (m_cur_docid >= m_last) {
                    m_cur_docid = cursor::document_bound;
                }
            }

            void DS2I_FLATTEN_FUNC move(uint64_t position)
            {
                auto val = m_docs_enum.move(position);
                m_cur_pos = val.first;
                m_cur_docid = val.second;
            }

            uint64_t docid() const
            {
                return m_cur_docid;
            }

            uint64_t DS2I_FLATTEN_FUNC freq()
            {
                return m_freqs_enum.move(m_cur_pos).second;
            }

            uint64_t position() const
            {
                return m_cur_pos;
            }

            uint64_t size() const
            {
                return m_docs_enum.size();
            }

            typename DocsSequence::enumerator const& docs_enum() const
            {
                return m_docs_enum;
            }

            typename FreqsSequence::enumerator const& freqs_enum() const
            {
                return m_freqs_enum;
            }

           private:
            friend class freq_index;

            Cursor(typename DocsSequence::enumerator docs_enum,
                   typename FreqsSequence::enumerator freqs_enum,
                   document_type last_document)
                : m_docs_enum(docs_enum), m_freqs_enum(freqs_enum), m_last(last_document)
            {
                reset();
            }

            uint64_t m_cur_pos;
            uint64_t m_cur_docid;
            typename DocsSequence::enumerator m_docs_enum;
            typename FreqsSequence::enumerator m_freqs_enum;
            document_type m_last;
        };

        class Posting_Range {
           public:
            using cursor_type = Cursor;
            using document_type = uint32_t;

            Posting_Range(freq_index const &index,
                          uint32_t term,
                          document_type first,
                          document_type last)
                : index_(index), term_(term), first_(first), last_(last)
            {
            }
            Posting_Range(Posting_Range &&) noexcept = default;
            Posting_Range &operator=(Posting_Range &&) noexcept = default;
            ~Posting_Range() = default;

            Posting_Range(Posting_Range const &) = delete;
            Posting_Range &operator=(Posting_Range const &) = delete;

            [[nodiscard]] auto size() const -> int64_t { return cursor().size(); } // TODO: clearly
            [[nodiscard]] auto first_document() const -> document_type { return first_; }
            [[nodiscard]] auto last_document() const -> document_type { return last_; }
            [[nodiscard]] auto cursor() const
            {
                auto docs_it = index_.m_docs_sequences.get(index_.m_params, term_);
                auto freqs_it = index_.m_freqs_sequences.get(index_.m_params, term_);

                uint64_t occurrences = read_gamma_nonzero(docs_it);
                uint64_t n = 1;
                if (occurrences > 1) {
                    n = docs_it.take(ceil_log2(occurrences + 1));
                }

                typename DocsSequence::enumerator docs_enum(index_.m_docs_sequences.bits(),
                                                            docs_it.position(),
                                                            index_.num_docs(),
                                                            n,
                                                            index_.m_params);

                typename FreqsSequence::enumerator freqs_enum(index_.m_freqs_sequences.bits(),
                                                              freqs_it.position(),
                                                              occurrences + 1,
                                                              n,
                                                              index_.m_params);

                auto cursor = Cursor(docs_enum, freqs_enum, last_);
                if (first_ > 0u) {
                    cursor.next_geq(first_);
                }
                return cursor;
            }
            [[nodiscard]] auto operator()(document_type low, document_type hi) const
                -> Posting_Range
            {
                Expects(low < hi and low >= first_ and hi <= last_);
                return Posting_Range(index_, term_, low, hi);
            }

           private:
            freq_index const &index_;
            uint32_t term_;
            document_type first_;
            document_type last_;
        };

        class document_enumerator {
        public:

            void reset()
            {
                m_cur_pos = 0;
                m_cur_docid = m_docs_enum.move(0).second;
            }

            void DS2I_FLATTEN_FUNC next()
            {
                auto val = m_docs_enum.next();
                m_cur_pos = val.first;
                m_cur_docid = val.second;
            }

            void DS2I_FLATTEN_FUNC next_geq(uint64_t lower_bound)
            {
                auto val = m_docs_enum.next_geq(lower_bound);
                m_cur_pos = val.first;
                m_cur_docid = val.second;
            }

            void DS2I_FLATTEN_FUNC move(uint64_t position)
            {
                auto val = m_docs_enum.move(position);
                m_cur_pos = val.first;
                m_cur_docid = val.second;
            }

            uint64_t docid() const
            {
                return m_cur_docid;
            }

            uint64_t DS2I_FLATTEN_FUNC freq()
            {
                return m_freqs_enum.move(m_cur_pos).second;
            }

            uint64_t position() const
            {
                return m_cur_pos;
            }

            uint64_t size() const
            {
                return m_docs_enum.size();
            }

            typename DocsSequence::enumerator const& docs_enum() const
            {
                return m_docs_enum;
            }

            typename FreqsSequence::enumerator const& freqs_enum() const
            {
                return m_freqs_enum;
            }

        private:
            friend class freq_index;

            document_enumerator(typename DocsSequence::enumerator docs_enum,
                                typename FreqsSequence::enumerator freqs_enum)
                : m_docs_enum(docs_enum)
                , m_freqs_enum(freqs_enum)
            {
                reset();
            }

            uint64_t m_cur_pos;
            uint64_t m_cur_docid;
            typename DocsSequence::enumerator m_docs_enum;
            typename FreqsSequence::enumerator m_freqs_enum;
        };

        [[nodiscard]] auto posting_range(uint32_t term) const -> Posting_Range
        {
            assert(term < size());
            return Posting_Range(*this, term, 0u, num_docs());
        }

        document_enumerator operator[](size_t i) const
        {
            assert(i < size());
            auto docs_it = m_docs_sequences.get(m_params, i);
            uint64_t occurrences = read_gamma_nonzero(docs_it);
            uint64_t n = 1;
            if (occurrences > 1) {
                n = docs_it.take(ceil_log2(occurrences + 1));
            }

            typename DocsSequence::enumerator docs_enum(m_docs_sequences.bits(),
                                                        docs_it.position(),
                                                        num_docs(), n,
                                                        m_params);

            auto freqs_it = m_freqs_sequences.get(m_params, i);
            typename FreqsSequence::enumerator freqs_enum(m_freqs_sequences.bits(),
                                                          freqs_it.position(),
                                                          occurrences + 1, n,
                                                          m_params);

            return document_enumerator(docs_enum, freqs_enum);
        }

        void warmup(size_t /* i */) const
        {
            // XXX implement this
        }

        global_parameters const& params() const
        {
            return m_params;
        }

        void swap(freq_index& other)
        {
            std::swap(m_params, other.m_params);
            std::swap(m_num_docs, other.m_num_docs);
            m_docs_sequences.swap(other.m_docs_sequences);
            m_freqs_sequences.swap(other.m_freqs_sequences);
        }

        template <typename Visitor>
        void map(Visitor& visit)
        {
            visit
                (m_params, "m_params")
                (m_num_docs, "m_num_docs")
                (m_docs_sequences, "m_docs_sequences")
                (m_freqs_sequences, "m_freqs_sequences")
                ;
        }

    private:
        global_parameters m_params;
        uint64_t m_num_docs;
        bitvector_collection m_docs_sequences;
        bitvector_collection m_freqs_sequences;
    };
}
