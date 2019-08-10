#include "macro.hpp"
#include "query/algorithm/and_query.hpp"

namespace pisa {

#define LOOP_BODY(R, DATA, T)                                                      \
    template std::vector<uint64_t> and_query::                                     \
    operator()<typename block_freq_index<T>::document_enumerator>(                 \
        std::vector<typename block_freq_index<T>::document_enumerator> && cursors, \
        uint64_t max_docid) const;
/**/
BOOST_PP_SEQ_FOR_EACH(LOOP_BODY, _, PISA_BLOCK_CODEC_TYPES);
#undef LOOP_BODY

} // namespace pisa
