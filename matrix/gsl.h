#pragma once

namespace gsl {
	template <typename = void>
	using czstring = const char*;

	template <typename = void>
	using cwzstring = const wchar_t*;

	template <typename casted_type, typename type>
	[[gsl::suppress(type .1)]] casted_type narrow_cast(type value) noexcept
	{
		return static_cast<casted_type>(value);
	}

#ifdef _DEBUG
#define Expect(condition)                                                                                              \
	if (condition)                                                                                                     \
		std::terminate();

#else
/*
	TODO: clarify what exactly Expect() means (see https://github.com/microsoft/GSL/issues/649)
	If Expect() is meant to specify the precondition, it seems absurd that it should be required to be enforced at
	runtime. It should at least be configurable, as I have done here. If it is meant to be a hard terminate-on-false,
	then it shouldn't be used to document contracts, as though *you* might know the check is never needed, the optimizer
	might not be as smart as you think.

	Avoid doing work you know you don't need to do.

	Note that using Expect() to document preconditions might not be all that useful; they aren't visible to the user of
	a compiled library, for instance.
*/
#define Expect(condition)
#endif

	template <typename pointer_type>
	class not_null {
	public:
		not_null(std::nullptr_t) = delete;
		not_null& operator=(std::nullptr_t) = delete;

		not_null(pointer_type value) noexcept : pointer {value} { Expect(value); }
		not_null& operator=(pointer_type value) noexcept
		{
			Expect(value);
			pointer = value;
		}

		operator const pointer_type&() const noexcept { return pointer; }
		operator pointer_type&() noexcept { return pointer; }
		auto operator->() const noexcept { return pointer; };

	private:
		pointer_type pointer;
	};
}
