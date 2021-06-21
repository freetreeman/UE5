// Copyright Epic Games, Inc. All Rights Reserved.

using HordeAgent.Parser;
using HordeAgent.Parser.Interfaces;
using HordeCommon;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace HordeAgent.Parser.Matchers
{
	/// <summary>
	/// Low-priority matcher for generic error strings like "warning:" and "error:"
	/// </summary>
	class GenericEventMatcher : ILogEventMatcher
	{
		/// <inheritdoc/>
		public LogEvent? Match(ILogCursor Cursor, ILogContext Context)
		{
			Match? Match;
			if (Cursor.TryMatch(@"^\s*(FATAL|fatal error):", out Match))
			{
				return new LogEventBuilder(Cursor).ToLogEvent(LogEventPriority.Low, LogLevel.Error, KnownLogEvents.Generic);
			}
			if (Cursor.TryMatch(@"(?<!\w)(?i)(WARNING|ERROR) ?(\([^)]+\)|\[[^\]]+\])?: ", out Match))
			{
				// Careful to match the first WARNING or ERROR in the line here.
				LogLevel Level = LogLevel.Error;
				if(Match.Groups[1].Value.Equals("WARNING", StringComparison.OrdinalIgnoreCase))
				{
					Level = LogLevel.Warning;
				}

				LogEventBuilder Builder = new LogEventBuilder(Cursor);
				while (Cursor.IsMatch(Builder.MaxOffset + 1, String.Format(@"^({0} | *$)", ExtractIndent(Cursor[0]!))))
				{
					Builder.MaxOffset++;
				}
				return Builder.ToLogEvent(LogEventPriority.Lowest, Level, KnownLogEvents.Generic);
			}
			if (Cursor.IsMatch(@"[Ee]rror [A-Z]\d+\s:"))
			{
				return new LogEventBuilder(Cursor).ToLogEvent(LogEventPriority.Lowest, LogLevel.Error, KnownLogEvents.Generic);
			}
			return null;
		}

		static string ExtractIndent(string Line)
		{
			int Length = 0;
			while (Length < Line.Length && Line[Length] == ' ')
			{
				Length++;
			}
			return new string(' ', Length);
		}
	}
}
