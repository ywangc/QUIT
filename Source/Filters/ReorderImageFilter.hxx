#ifndef REORDERIMAGEFILTER_HXX
#define REORDERIMAGEFILTER_HXX

namespace itk {

template<typename TImage>
void ReorderImageFilter<TImage>::GenerateOutputInformation() {
    //std::cout << __PRETTY_FUNCTION__ << std::endl;
    Superclass::GenerateOutputInformation();
    //std::cout << "END " << __PRETTY_FUNCTION__ << std::endl;
}

template<typename TImage>
void ReorderImageFilter<TImage>::GenerateData() {
    //std::cout << __PRETTY_FUNCTION__ << std::endl;
    this->AllocateOutputs();
    typename TImage::ConstPointer input = this->GetInput();
    typename TImage::Pointer output = this->GetOutput();

    TRegion inRegion = input->GetLargestPossibleRegion();
    TRegion outRegion = inRegion;
    const size_t lastDim = TImage::ImageDimension - 1;
    m_fullsize = inRegion.GetSize()[lastDim];

    if (m_blocksize == 0)
        m_blocksize = m_fullsize;

    if ((m_fullsize % m_blocksize) != 0) {
        throw(std::runtime_error("Fullsize must be an integer multiple of blocksize."));
    }
    if ((m_blocksize % m_stride) != 0) {
        throw(std::runtime_error("Blocksize (" + std::to_string(m_blocksize) + ") must be an integer multiple of stride (" + std::to_string(m_stride) + ")."));
    }
    m_blocks = m_fullsize / m_blocksize;

    inRegion.GetModifiableSize()[lastDim] = 1;
    outRegion.GetModifiableSize()[lastDim] = 1;
    size_t o = 0; // Tracks the output volume
    for (size_t b = 0; b < m_fullsize; b += m_blocksize) { // b Tracks the start of each block
        for (size_t is = b; is < (b + m_stride); is++) { // Tracks the start of a set
            for (size_t i = is; i < (b + m_blocksize); i += m_stride) { // i Tracks the input volume within block
                // Copy the input to the output
                inRegion.GetModifiableIndex()[lastDim] = i;
                outRegion.GetModifiableIndex()[lastDim] = o;
                ImageAlgorithm::Copy(input.GetPointer(), output.GetPointer(), inRegion, outRegion);
                o++;
            }
        }
    }
    //std::cout << "END" << __PRETTY_FUNCTION__ << std::endl;
}

} // End namespace itk

#endif // Define REORDERIMAGEFILTER_HXX
