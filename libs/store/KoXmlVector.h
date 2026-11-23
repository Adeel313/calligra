/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2005-2006 Ariya Hidayat <ariya@kde.org>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#ifndef KO_XMLVECTOR_H
#define KO_XMLVECTOR_H

// comment it to test this class without the compression
#define KOXMLVECTOR_USE_LZF

#ifdef KOXMLVECTOR_USE_LZF
#include "KoLZF.h"
#endif

#include <QBuffer>
#include <QByteArray>
#include <QDataStream>
#include <QVector>

#include <algorithm>

/**
 * KoXmlVector
 *
 * similar to QVector, but using LZF compression to save memory space
 * this class is however not reentrant
 *
 * Needs to be used like this, otherwise will crash:
 * <ul>
 * <li>add content with newItem()</li>
 * <li>finish adding content with squeeze()</li>
 * <li>just read content with operator[]</li>
 * </ul>
 *
 * @param uncompressedItemCount when number of buffered items reach this,
 *      compression will start small value will give better memory usage at the
 *      cost of speed bigger value will be better in term of speed, but use
 *      more memory
 */
template<typename T, int uncompressedItemCount = 256, int reservedBufferSize = 1024 * 1024, int retainedBytes = 0>
class KoXmlVector
{
private:
    unsigned m_totalItems;
    QVector<unsigned> m_startIndex;
    QVector<QByteArray> m_blocks;
    // An optional bounded prefix stays in its original form. This avoids
    // serializing, compressing and reconstructing the initial working set.
    QVector<QVector<T>> m_retainedBlocks;
    unsigned m_retainedItems = 0;

    mutable unsigned m_bufferStartIndex;
    mutable QVector<T> m_bufferItems;

    struct DecodedBlock
    {
        unsigned startIndex = 0;
        unsigned lastUse = 0;
        QVector<T> items;
    };
    static constexpr int decodedBlockCacheSize = 8;
    mutable QVector<DecodedBlock> m_decodedBlocks;
    mutable unsigned m_useCounter = 0;
    mutable int m_lastDecodedBlock = -1;
    mutable QByteArray m_decodeBuffer;

protected:
    /**
     * decode the stored block holding the given item index into the cache
     * (least recently used block is replaced when the cache is full)
     */
    const DecodedBlock &decodeBlock(unsigned index) const
    {
        if (m_lastDecodedBlock >= 0) {
            DecodedBlock &block = m_decodedBlocks[m_lastDecodedBlock];
            if (index >= block.startIndex && index - block.startIndex < (unsigned)block.items.count()) {
                block.lastUse = ++m_useCounter;
                return block;
            }
        }
        for (int i = 0; i < m_decodedBlocks.count(); ++i) {
            DecodedBlock &block = m_decodedBlocks[i];
            if (index >= block.startIndex && index - block.startIndex < (unsigned)block.items.count()) {
                block.lastUse = ++m_useCounter;
                m_lastDecodedBlock = i;
                return block;
            }
        }

        const auto next = std::upper_bound(m_startIndex.cbegin(), m_startIndex.cend(), index);
        const auto loc = next - m_startIndex.cbegin() - 1;

        DecodedBlock *slot = nullptr;
        if (m_decodedBlocks.count() < decodedBlockCacheSize) {
            m_decodedBlocks.append(DecodedBlock());
            slot = &m_decodedBlocks.last();
        } else {
            slot = &m_decodedBlocks[0];
            for (DecodedBlock &block : m_decodedBlocks)
                if (block.lastUse < slot->lastUse)
                    slot = &block;
        }

#ifdef KOXMLVECTOR_USE_LZF
        KoLZF::decompress(m_blocks[loc], m_decodeBuffer);
#else
        m_decodeBuffer = m_blocks[loc];
#endif
        QBuffer buffer(&m_decodeBuffer);
        buffer.open(QIODevice::ReadOnly);
        QDataStream in(&buffer);
        slot->items.clear();
        in >> slot->items;
        slot->startIndex = m_startIndex[loc];
        slot->lastUse = ++m_useCounter;
        m_lastDecodedBlock = int(slot - m_decodedBlocks.data());
        return *slot;
    }

    /**
     * store data in the buffer to main m_blocks
     */
    void storeBuffer()
    {
        if (m_bufferItems.isEmpty())
            return;
        if constexpr (retainedBytes > 0) {
            if (m_blocks.isEmpty()
                && quint64(m_retainedItems + m_bufferItems.size()) * sizeof(T) <= retainedBytes) {
                m_bufferStartIndex += m_bufferItems.size();
                m_retainedItems += m_bufferItems.size();
                m_retainedBlocks.append(std::move(m_bufferItems));
                return;
            }
        }
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        QDataStream out(&buffer);
        out << m_bufferItems;

        m_startIndex.append(m_bufferStartIndex);
#ifdef KOXMLVECTOR_USE_LZF
        m_blocks.append(KoLZF::compress(buffer.data()));
#else
        m_blocks.append(buffer.data());
#endif

        m_bufferStartIndex += m_bufferItems.count();
        m_bufferItems.clear();
    }

public:
    inline KoXmlVector()
        : m_totalItems(0)
        , m_bufferStartIndex(0) { };

    void clear()
    {
        m_totalItems = 0;
        m_startIndex.clear();
        m_blocks.clear();
        m_retainedBlocks.clear();
        m_retainedItems = 0;

        m_bufferStartIndex = 0;
        m_bufferItems.clear();
        m_decodedBlocks.clear();
        m_lastDecodedBlock = -1;
        m_decodeBuffer.reserve(reservedBufferSize);
    }

    inline int count() const
    {
        return (int)m_totalItems;
    }
    inline int size() const
    {
        return (int)m_totalItems;
    }
    inline bool isEmpty() const
    {
        return m_totalItems == 0;
    }

    /**
     * append a new item
     * WARNING: use the return value as soon as possible
     * it may be invalid if another function is invoked
     */
    T &newItem()
    {
        // buffer full?
        if (m_bufferItems.count() >= uncompressedItemCount - 1)
            storeBuffer();

        ++m_totalItems;
        return m_bufferItems.emplaceBack();
    }

    /**
     * WARNING: use the return value as soon as possible
     * it may be invalid if another function is invoked
     */
    const T &operator[](int i) const
    {
        const unsigned index = (unsigned)i;
        if constexpr (retainedBytes > 0) {
            if (index < m_retainedItems)
                return m_retainedBlocks.at(index / (uncompressedItemCount - 1)).at(index % (uncompressedItemCount - 1));
        }
        if (index >= m_bufferStartIndex && index - m_bufferStartIndex < (unsigned)m_bufferItems.count())
            return m_bufferItems[index - m_bufferStartIndex];

        const DecodedBlock &block = decodeBlock(index);
        return block.items[index - block.startIndex];
    }

    /**
     * optimize memory usage
     * will INVALIDATE all references to the buffer
     */
    void squeeze()
    {
        storeBuffer();
    }
};

#endif
