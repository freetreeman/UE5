// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;
using TemplateRefId = HordeServer.Utilities.StringId<HordeServer.Models.TemplateRef>;

namespace HordeServer.Api
{
	/// <summary>
	/// Identifies a particular changelist and job
	/// </summary>
	public class GetIssueStepResponse
	{
		/// <summary>
		/// The changelist number
		/// </summary>
		public int Change { get; set; }

		/// <summary>
		/// Severity of the issue in this step
		/// </summary>
		public IssueSeverity Severity { get; set; }

		/// <summary>
		/// Name of the job containing this step
		/// </summary>
		public string JobName { get; set; }

		/// <summary>
		/// The unique job id
		/// </summary>
		public string JobId { get; set; }

		/// <summary>
		/// The unique batch id
		/// </summary>
		public string BatchId { get; set; }

		/// <summary>
		/// The unique step id
		/// </summary>
		public string StepId { get; set; }

		/// <summary>
		/// Time at which the step ran
		/// </summary>
		public DateTime StepTime { get; set; }

		/// <summary>
		/// The unique log id
		/// </summary>
		public string? LogId { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="IssueStep">The issue step to construct from</param>
		public GetIssueStepResponse(IIssueStep IssueStep)
		{
			this.Change = IssueStep.Change;
			this.Severity = IssueStep.Severity;
			this.JobName = IssueStep.JobName;
			this.JobId = IssueStep.JobId.ToString();
			this.BatchId = IssueStep.BatchId.ToString();
			this.StepId = IssueStep.StepId.ToString();
			this.StepTime = IssueStep.StepTime;
			this.LogId = IssueStep.LogId?.ToString();
		}
	}

	/// <summary>
	/// Trace of a set of node failures across multiple steps
	/// </summary>
	public class GetIssueNodeResponse
	{
		/// <summary>
		/// The template containing this step
		/// </summary>
		public string TemplateId { get; set; }

		/// <summary>
		/// Name of the step
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// The previous build 
		/// </summary>
		public GetIssueStepResponse? LastSuccess { get; set; }

		/// <summary>
		/// The failing builds for a particular event
		/// </summary>
		public List<GetIssueStepResponse> Steps { get; set; }

		/// <summary>
		/// The following successful build
		/// </summary>
		public GetIssueStepResponse? NextSuccess { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Span">The node to construct from</param>
		/// <param name="Steps">Failing steps for this span</param>
		public GetIssueNodeResponse(IIssueSpan Span, List<IIssueStep> Steps)
		{
			this.Name = Span.NodeName;
			this.TemplateId = Span.TemplateRefId.ToString();
			this.LastSuccess = (Span.LastSuccess != null) ? new GetIssueStepResponse(Span.LastSuccess) : null;
			this.Steps = Steps.ConvertAll(x => new GetIssueStepResponse(x));
			this.NextSuccess = (Span.NextSuccess != null) ? new GetIssueStepResponse(Span.NextSuccess) : null;
		}
	}

	/// <summary>
	/// Information about a particular step
	/// </summary>
	public class GetIssueStreamResponse
	{
		/// <summary>
		/// Unique id of the stream
		/// </summary>
		public string StreamId { get; set; }

		/// <summary>
		/// Minimum changelist affected by this issue (ie. last successful build)
		/// </summary>
		public int? MinChange { get; set; }

		/// <summary>
		/// Maximum changelist affected by this issue (ie. next successful build)
		/// </summary>
		public int? MaxChange { get; set; }

		/// <summary>
		/// Map of steps to (event signature id -> trace id)
		/// </summary>
		public List<GetIssueNodeResponse> Nodes { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="StreamId">The stream to construct from</param>
		/// <param name="Spans">List of spans for the given stream</param>
		/// <param name="Steps">List of steps for the given stream</param>
		public GetIssueStreamResponse(StreamId StreamId, List<IIssueSpan> Spans, List<IIssueStep> Steps)
		{
			this.StreamId = StreamId.ToString();

			foreach (IIssueSpan Span in Spans)
			{
				if (Span.LastSuccess != null && (MinChange == null || Span.LastSuccess.Change < MinChange.Value))
				{
					MinChange = Span.LastSuccess.Change;
				}
				if (Span.NextSuccess != null && (MaxChange == null || Span.NextSuccess.Change > MaxChange.Value))
				{
					MaxChange = Span.NextSuccess.Change;
				}
			}

			this.Nodes = Spans.ConvertAll(x => new GetIssueNodeResponse(x, Steps.Where(y => y.SpanId == x.Id).ToList()));
		}
	}

	/// <summary>
	/// Outcome of a particular build
	/// </summary>
	public enum IssueBuildOutcome
	{
		/// <summary>
		/// Unknown outcome
		/// </summary>
		Unknown,

		/// <summary>
		/// Build succeeded
		/// </summary>
		Success,

		/// <summary>
		/// Build failed
		/// </summary>
		Error,

		/// <summary>
		/// Build finished with warnings
		/// </summary>
		Warning,
	}

	/// <summary>
	/// Information about a suspect changelist that may have caused an issue
	/// </summary>
	public class GetIssueSuspectResponse
	{
		/// <summary>
		/// Number of the changelist that was submitted
		/// </summary>
		public int Change { get; set; }

		/// <summary>
		/// Author of the changelist
		/// </summary>
		public string Author { get; set; }

		/// <summary>
		/// The originating change
		/// </summary>
		public int? OriginatingChange { get; set; }

		/// <summary>
		/// Time at which the user declined this issue
		/// </summary>
		public DateTime? DeclinedAt { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="IssueSuspect">The object to construct from</param>
		public GetIssueSuspectResponse(IIssueSpanSuspect IssueSuspect)
		{
			this.Change = IssueSuspect.Change;
			this.Author = IssueSuspect.Author;
			this.OriginatingChange = IssueSuspect.OriginatingChange;
		}
	}

	/// <summary>
	/// Information about a diagnostic
	/// </summary>
	public class GetIssueDiagnosticResponse
	{
		/// <summary>
		/// The corresponding build id
		/// </summary>
		public long? BuildId { get; set; }

		/// <summary>
		/// Message for the diagnostic
		/// </summary>
		public string Message { get; set; }

		/// <summary>
		/// Link to the error
		/// </summary>
		public Uri Url { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="BuildId">The corresponding build id</param>
		/// <param name="Message">Message for the diagnostic</param>
		/// <param name="Url">Link to the diagnostic</param>
		public GetIssueDiagnosticResponse(long? BuildId, string Message, Uri Url)
		{
			this.BuildId = BuildId;
			this.Message = Message;
			this.Url = Url;
		}
	}

	/// <summary>
	/// Information about a template affected by an issue
	/// </summary>
	public class GetIssueAffectedTemplateResponse
	{
		/// <summary>
		/// The template id
		/// </summary>
		public string TemplateId { get; set; }

		/// <summary>
		/// The template name
		/// </summary>
		public string TemplateName { get; set; }

		/// <summary>
		/// Whether it has been resolved or not
		/// </summary>
		public bool Resolved { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="TemplateId"></param>
		/// <param name="TemplateName"></param>
		/// <param name="Resolved"></param>
		public GetIssueAffectedTemplateResponse(string TemplateId, string TemplateName, bool Resolved)
		{
			this.TemplateId = TemplateId;
			this.TemplateName = TemplateName;
			this.Resolved = Resolved;
		}
	}

	/// <summary>
	/// Summary for the state of a stream in an issue
	/// </summary>
	public class GetIssueAffectedStreamResponse
	{
		/// <summary>
		/// Id of the stream
		/// </summary>
		public string StreamId { get; set; }

		/// <summary>
		/// Name of the stream
		/// </summary>
		public string StreamName { get; set; }

		/// <summary>
		/// Whether the issue has been resolved in this stream
		/// </summary>
		public bool Resolved { get; set; }

		/// <summary>
		/// The affected templates
		/// </summary>
		public List<GetIssueAffectedTemplateResponse> AffectedTemplates { get; set; }

		/// <summary>
		/// List of affected template ids
		/// </summary>
		public List<string> TemplateIds { get; set; }

		/// <summary>
		/// List of resolved template ids
		/// </summary>
		public List<string> ResolvedTemplateIds { get; set; }

		/// <summary>
		/// List of unresolved template ids
		/// </summary>
		public List<string> UnresolvedTemplateIds { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Stream"></param>
		/// <param name="Spans">The spans to construct from</param>
		public GetIssueAffectedStreamResponse(IStream? Stream, IEnumerable<IIssueSpan> Spans)
		{
			IIssueSpan FirstSpan = Spans.First();
			this.StreamId = FirstSpan.StreamId.ToString();
			this.StreamName = FirstSpan.StreamName;
			this.Resolved = Spans.All(x => x.NextSuccess != null);

			this.AffectedTemplates = new List<GetIssueAffectedTemplateResponse>();
			foreach (IGrouping<TemplateRefId, IIssueSpan> Template in Spans.GroupBy(x => x.TemplateRefId))
			{
				string TemplateName = Template.Key.ToString();
				if (Stream != null && Stream.Templates.TryGetValue(Template.Key, out TemplateRef? TemplateRef))
				{
					TemplateName = TemplateRef.Name;
				}
				this.AffectedTemplates.Add(new GetIssueAffectedTemplateResponse(Template.Key.ToString(), TemplateName, Template.All(x => x.NextSuccess != null)));
			}

			HashSet<TemplateRefId> TemplateIdsSet = new HashSet<TemplateRefId>(Spans.Select(x => x.TemplateRefId));
			this.TemplateIds = TemplateIdsSet.Select(x => x.ToString()).ToList();

			HashSet<TemplateRefId> UnresolvedTemplateIdsSet = new HashSet<TemplateRefId>(Spans.Where(x => x.NextSuccess == null).Select(x => x.TemplateRefId));
			this.UnresolvedTemplateIds = UnresolvedTemplateIdsSet.Select(x => x.ToString()).ToList();
			this.ResolvedTemplateIds = TemplateIdsSet.Except(UnresolvedTemplateIdsSet).Select(x => x.ToString()).ToList();
		}
	}

	/// <summary>
	/// Stores information about a build health issue
	/// </summary>
	public class GetIssueResponse
	{
		/// <summary>
		/// The unique object id
		/// </summary>
		public int Id { get; set; }

		/// <summary>
		/// Time at which the issue was created
		/// </summary>
		public DateTime CreatedAt { get; set; }

		/// <summary>
		/// Time at which the issue was retrieved
		/// </summary>
		public DateTime RetrievedAt { get; set; }

		/// <summary>
		/// The associated project for the issue
		/// </summary>
		public string? Project { get; set; }

		/// <summary>
		/// The summary text for this issue
		/// </summary>
		public string Summary { get; set; }

		/// <summary>
		/// Details text describing the issue
		/// </summary>
		public string? Details { get; set; }

		/// <summary>
		/// Severity of this issue
		/// </summary>
		public IssueSeverity Severity { get; set; }

		/// <summary>
		/// Owner of the issue
		/// </summary>
		public string? Owner { get; set; }

		/// <summary>
		/// User id of the owner
		/// </summary>
		public string? OwnerId { get; set; }

		/// <summary>
		/// User that nominated the current owner
		/// </summary>
		public string? NominatedBy { get; set; }

		/// <summary>
		/// Time that the issue was acknowledged
		/// </summary>
		public DateTime? AcknowledgedAt { get; set; }

		/// <summary>
		/// Changelist that fixed this issue
		/// </summary>
		public int? FixChange { get; set; }

		/// <summary>
		/// Time at which the issue was resolved
		/// </summary>
		public DateTime? ResolvedAt { get; set; }

		/// <summary>
		/// Name of the user that resolved the issue
		/// </summary>
		public string? ResolvedBy { get; set; }

		/// <summary>
		/// User id of the person that resolved the issue
		/// </summary>
		public string? ResolvedById { get; set; }

		/// <summary>
		/// Time at which the issue was verified
		/// </summary>
		public DateTime? VerifiedAt { get; set; }

		/// <summary>
		/// Time that the issue was last seen
		/// </summary>
		public DateTime LastSeenAt { get; set; }

		/// <summary>
		/// List of stream paths affected by this issue
		/// </summary>
		public List<string> Streams { get; set; }

		/// <summary>
		/// List of affected stream ids
		/// </summary>
		public List<string> ResolvedStreams { get; set; }

		/// <summary>
		/// List of unresolved streams
		/// </summary>
		public List<string> UnresolvedStreams { get; set; }

		/// <summary>
		/// List of affected streams
		/// </summary>
		public List<GetIssueAffectedStreamResponse> AffectedStreams { get; set; }

		/// <summary>
		/// Most likely suspects for causing this issue, deprecated
		/// </summary>
		public List<string> PrimarySuspects { get; set; }

		/// <summary>
		/// User ids of the most likely suspects
		/// </summary>
		public List<string> PrimarySuspectIds { get; set; }

		/// <summary>
		/// Whether to show alerts for this issue
		/// </summary>
		public bool ShowDesktopAlerts { get; set; }

		/// <summary>
		/// Constructs a new issue
		/// </summary>
		/// <param name="Details">Issue to construct from</param>
		/// <param name="AffectedStreams">The affected streams</param>
		/// <param name="ShowDesktopAlerts">Whether to show alerts for this issue</param>
		public GetIssueResponse(IIssueDetails Details, List<GetIssueAffectedStreamResponse> AffectedStreams, bool ShowDesktopAlerts)
		{
			IIssue Issue = Details.Issue;
			this.Id = Issue.Id;
			this.CreatedAt = Issue.CreatedAt;
			this.RetrievedAt = DateTime.UtcNow;
			this.Summary = String.IsNullOrEmpty(Issue.UserSummary)? Issue.Summary : Issue.UserSummary;
			this.Severity = Issue.Severity;
			this.Owner = Details.Owner?.Login;
			this.OwnerId = (Details.Owner == null)? null : Details.Owner.Id.ToString();
			this.NominatedBy = Details.NominatedBy?.Login;
			this.AcknowledgedAt = Issue.AcknowledgedAt;
			this.FixChange = Issue.FixChange;
			this.ResolvedAt = Issue.ResolvedAt;
			this.ResolvedBy = Details.ResolvedBy?.Login;
			this.ResolvedById = (Details.ResolvedBy == null) ? null : Details.ResolvedBy.Id.ToString();
			this.VerifiedAt = Issue.VerifiedAt;
			this.LastSeenAt = Issue.LastSeenAt;
			this.Streams = Details.Spans.Select(x => x.StreamName).Distinct().ToList()!;
			this.ResolvedStreams = new List<string>();
			this.UnresolvedStreams = new List<string>();
			this.AffectedStreams = AffectedStreams;
			foreach (IGrouping<StreamId, IIssueSpan> Stream in Details.Spans.GroupBy(x => x.StreamId))
			{
				if (Stream.All(x => x.NextSuccess != null))
				{
					this.ResolvedStreams.Add(Stream.Key.ToString());
				}
				else
				{
					this.UnresolvedStreams.Add(Stream.Key.ToString());
				}
			}
			this.PrimarySuspects = Details.SuspectUsers.Where(x => x.Login != null).Select(x => x.Login).ToList();
			this.PrimarySuspectIds= Details.SuspectUsers.Select(x => x.Id.ToString()).ToList();
			this.ShowDesktopAlerts = ShowDesktopAlerts;
		}
	}

	/// <summary>
	/// Request an issue to be updated
	/// </summary>
	public class UpdateIssueRequest
	{
		/// <summary>
		/// Summary of the issue
		/// </summary>
		public string? Summary { get; set; }

		/// <summary>
		/// New user id for owner of the issue
		/// </summary>
		public string? OwnerId { get; set; }

		/// <summary>
		/// User id that nominated the new owner
		/// </summary>
		public string? NominatedById { get; set; }

		/// <summary>
		/// Whether the issue has been acknowledged
		/// </summary>
		public bool? Acknowledged { get; set; }

		/// <summary>
		/// Whether the user has declined this issue
		/// </summary>
		public bool? Declined { get; set; }

		/// <summary>
		/// The change at which the issue is claimed fixed. 0 = not fixed, -1 = systemic issue.
		/// </summary>
		public int? FixChange { get; set; }

		/// <summary>
		/// Whether the issue should be marked as resolved
		/// </summary>
		public bool? Resolved { get; set; }
	}
}
