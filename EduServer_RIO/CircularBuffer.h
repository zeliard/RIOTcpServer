#pragma once


class CircularBuffer
{
public:

	CircularBuffer(char* bufStart, size_t capacity) : mBuffer(bufStart), mBufferEnd(bufStart+capacity)
	{
		Reset();
	}

	~CircularBuffer()
	{}

	void Reset()
	{
		mARegionPointer = mBuffer;
		mBRegionPointer = nullptr;
		mARegionSize = 0;
		mBRegionSize = 0;
	}


	void Remove(size_t len) ;

	size_t GetFreeSpaceSize()
	{
		if ( mBRegionPointer != nullptr )
			return GetBFreeSpace() ;
		else
		{
			if ( GetAFreeSpace() < GetSpaceBeforeA() )
			{
				AllocateB() ;
				return GetBFreeSpace() ;
			}
			else
				return GetAFreeSpace() ;
		}
	}

	size_t GetStoredSize() const
	{
		return mARegionSize + mBRegionSize ;
	}

	size_t GetContiguiousBytes() const 
	{
		if ( mARegionSize > 0 )
			return mARegionSize ;
		else
			return mBRegionSize ;
	}

	ULONG GetWritableOffset() const
	{
		if (mBRegionPointer != nullptr)
			return static_cast<ULONG>(mBRegionPointer + mBRegionSize - mBuffer);
		else
			return static_cast<ULONG>(mARegionPointer + mARegionSize - mBuffer);
	}


	void Commit(size_t len)
	{
		if ( mBRegionPointer != nullptr )
			mBRegionSize += len ;
		else
			mARegionSize += len ;
	}


	ULONG GetReadableOffset() const
	{
		if (mARegionSize > 0)
			return static_cast<ULONG>(mARegionPointer-mBuffer);
		else
			return static_cast<ULONG>(mBRegionPointer-mBuffer);
	}


private:

	void AllocateB()
	{
		mBRegionPointer = mBuffer ;
	}

	size_t GetAFreeSpace() const
	{ 
		return (mBufferEnd - mARegionPointer - mARegionSize) ; 
	}

	size_t GetSpaceBeforeA() const
	{ 
		return (mARegionPointer - mBuffer) ; 
	}


	size_t GetBFreeSpace() const
	{ 
		if (mBRegionPointer == nullptr)
			return 0 ; 

		return (mARegionPointer - mBRegionPointer - mBRegionSize) ; 
	}

private:

	char* const mBuffer ;
	char* const mBufferEnd ;

	char*	mARegionPointer ;
	size_t	mARegionSize ;

	char*	mBRegionPointer ;
	size_t	mBRegionSize ;

} ;
