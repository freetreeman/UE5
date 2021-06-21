// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Api;
using HordeCommon;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Tasks.Impl;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Primitives;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net;
using System.Runtime;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Web;

using PoolId = HordeServer.Utilities.StringId<HordeServer.Models.IPool>;
using ProjectId = HordeServer.Utilities.StringId<HordeServer.Models.IProject>;
using TemplateRefId = HordeServer.Utilities.StringId<HordeServer.Models.TemplateRef>;
using AgentSoftwareVersion = HordeServer.Utilities.StringId<HordeServer.Collections.IAgentSoftwareCollection>;

using IStream = HordeServer.Models.IStream;
using MongoDB.Driver;
using System.Text.Json;
using System.Threading;
using HordeServer.Services.Impl;
using Microsoft.Net.Http.Headers;
using HordeServer.Storage;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Options;
using StatsdClient;
using P4Debugging = Perforce.P4.P4Debugging; 

namespace HordeServer.Controllers
{
	/// <summary>
	/// Public endpoints for the debug controller
	/// </summary>
	[ApiController]
	[Route("[controller]")]
	public class PublicDebugController : ControllerBase
	{
		/// <summary>
		/// The connection tracker service singleton
		/// </summary>
		RequestTrackerService RequestTrackerService;

		IHostApplicationLifetime ApplicationLifetime;

		IDogStatsd DogStatsd;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="RequestTrackerService"></param>
		/// <param name="ApplicationLifetime"></param>
		/// <param name="DogStatsd"></param>
		public PublicDebugController(RequestTrackerService RequestTrackerService, IHostApplicationLifetime ApplicationLifetime, IDogStatsd DogStatsd)
		{
			this.RequestTrackerService = RequestTrackerService;
			this.ApplicationLifetime = ApplicationLifetime;
			this.DogStatsd = DogStatsd;
		}

		/// <summary>
		/// Prints all the headers for the incoming request
		/// </summary>
		/// <returns>Http result</returns>
		[HttpGet]
		[Route("/api/v1/debug/headers")]
		public IActionResult GetRequestHeaders()
		{
			StringBuilder Content = new StringBuilder();
			Content.AppendLine("<html><body><pre>");
			foreach (KeyValuePair<string, StringValues> Pair in HttpContext.Request.Headers)
			{
				foreach (string Value in Pair.Value)
				{
					Content.AppendLine(HttpUtility.HtmlEncode($"{Pair.Key}: {Value}"));
				}
			}
			Content.Append("</pre></body></html>");
			return new ContentResult { ContentType = "text/html", StatusCode = (int)HttpStatusCode.OK, Content = Content.ToString() };
		}

		/// <summary>
		/// Waits specified number of milliseconds and then returns a response
		/// Used for testing timeouts proxy settings.
		/// </summary>
		/// <returns>Http result</returns>
		[HttpGet]
		[Route("/api/v1/debug/wait")]
		public async Task<ActionResult> GetAndWait([FromQuery] int WaitTimeMs = 1000)
		{
			await Task.Delay(WaitTimeMs);
			string Content = $"Waited {WaitTimeMs} ms. " + new Random().Next(0, 10000000);
			return new ContentResult { ContentType = "text/plain", StatusCode = (int)HttpStatusCode.OK, Content = Content };
		}

		/// <summary>
		/// Waits specified number of milliseconds and then throws an exception
		/// Used for testing graceful shutdown and interruption of outstanding requests.
		/// </summary>
		/// <returns>Http result</returns>
		[HttpGet]
		[Route("/api/v1/debug/exception")]
		public async Task<ActionResult> ThrowException([FromQuery] int WaitTimeMs = 0)
		{
			await Task.Delay(WaitTimeMs);
			throw new Exception("Test exception triggered by debug controller!");
		}

		/// <summary>
		/// Trigger an increment of a DogStatsd metric
		/// </summary>
		/// <returns>Http result</returns>
		[HttpGet]
		[Route("/api/v1/debug/metric")]
		public ActionResult TriggerMetric([FromQuery] int Value = 10)
		{
			DogStatsd.Increment("hordeMetricTest", Value);
			return Ok("Incremented metric 'hordeMetricTest' Type: " + DogStatsd.GetType());
		}

		/// <summary>
		/// Display metrics related to the .NET runtime
		/// </summary>
		/// <returns>Http result</returns>
		[HttpGet]
		[Route("/api/v1/debug/dotnet-metrics")]
		public ActionResult DotNetMetrics()
		{
			ThreadPool.GetMaxThreads(out int MaxWorkerThreads, out int MaxIoThreads);
			ThreadPool.GetAvailableThreads(out int FreeWorkerThreads, out int FreeIoThreads);
			ThreadPool.GetMinThreads(out int MinWorkerThreads, out int MinIoThreads);

			int BusyIoThreads = MaxIoThreads - FreeIoThreads;
			int BusyWorkerThreads = MaxWorkerThreads - FreeWorkerThreads;

			StringBuilder Content = new StringBuilder();
			Content.AppendLine("Threads:");
			Content.AppendLine("-------------------------------------------------------------");
			Content.AppendLine("Worker busy={0,-5} free={1,-5} min={2,-5} max={3,-5}", BusyWorkerThreads, FreeWorkerThreads, MinWorkerThreads, MaxWorkerThreads);
			Content.AppendLine("  IOCP busy={0,-5} free={1,-5} min={2,-5} max={3,-5}", BusyIoThreads, FreeIoThreads, MinIoThreads, MaxWorkerThreads);


			NumberFormatInfo Nfi = (NumberFormatInfo)CultureInfo.InvariantCulture.NumberFormat.Clone();
			Nfi.NumberGroupSeparator = " ";

			string FormatBytes(long Number)
			{
				return (Number / 1024 / 1024).ToString("#,0", Nfi) + " MB";
			}

			GCMemoryInfo GcMemoryInfo = GC.GetGCMemoryInfo();
			Content.AppendLine("");
			Content.AppendLine("");
			Content.AppendLine("Garbage collection (GC):");
			Content.AppendLine("-------------------------------------------------------------");
			Content.AppendLine("              Latency mode: " + GCSettings.LatencyMode);
			Content.AppendLine("              Is server GC: " + GCSettings.IsServerGC);
			Content.AppendLine("              Total memory: " + FormatBytes(GC.GetTotalMemory(false)));
			Content.AppendLine("           Total allocated: " + FormatBytes(GC.GetTotalAllocatedBytes(false)));
			Content.AppendLine("                 Heap size: " + FormatBytes(GcMemoryInfo.HeapSizeBytes));
			Content.AppendLine("                Fragmented: " + FormatBytes(GcMemoryInfo.FragmentedBytes));
			Content.AppendLine("               Memory Load: " + FormatBytes(GcMemoryInfo.MemoryLoadBytes));
			Content.AppendLine("    Total available memory: " + FormatBytes(GcMemoryInfo.TotalAvailableMemoryBytes));
			Content.AppendLine("High memory load threshold: " + FormatBytes(GcMemoryInfo.HighMemoryLoadThresholdBytes));

			return Ok(Content.ToString());
		}

		/// <summary>
		/// Force a full GC of all generations
		/// </summary>
		/// <returns>Prints time taken in ms</returns>
		[HttpGet]
		[Route("/api/v1/debug/force-gc")]
		public ActionResult ForceTriggerGc()
		{
			Stopwatch Timer = new Stopwatch();
			Timer.Start();
			GC.Collect();
			Timer.Stop();
			return Ok($"Time taken: {Timer.Elapsed.TotalMilliseconds} ms");
		}

		/// <summary>
		/// Lists requests in progress
		/// </summary>
		/// <returns>HTML result</returns>
		[HttpGet]
		[Route("/api/v1/debug/requests-in-progress")]
		public ActionResult GetRequestsInProgress()
		{
			StringBuilder Content = new StringBuilder();
			Content.AppendLine("<html><body>");
			Content.AppendLine("<h1>Requests in progress</h1>");
			Content.AppendLine("<table border=\"1\">");
			Content.AppendLine("<tr>");
			Content.AppendLine("<th>Request Trace ID</th>");
			Content.AppendLine("<th>Path</th>");
			Content.AppendLine("<th>Started At</th>");
			Content.AppendLine("<th>Age</th>");
			Content.AppendLine("</tr>");

			List<KeyValuePair<string, TrackedRequest>> Requests = RequestTrackerService.GetRequestsInProgress().ToList();
			Requests.Sort((A, B) => A.Value.StartedAt.CompareTo(B.Value.StartedAt));

			foreach (KeyValuePair<string, TrackedRequest> Entry in Requests)
			{
				Content.Append("<tr>");
				Content.AppendLine($"<td>{Entry.Key}</td>");
				Content.AppendLine($"<td>{Entry.Value.Request.Path}</td>");
				Content.AppendLine($"<td>{Entry.Value.StartedAt}</td>");
				Content.AppendLine($"<td>{Entry.Value.GetTimeSinceStartInMs()} ms</td>");
				Content.Append("</tr>");
			}
			Content.Append("</table>\n</body>\n</html>");

			return new ContentResult { ContentType = "text/html", StatusCode = (int)HttpStatusCode.OK, Content = Content.ToString() };
		}

		/*
		// Used during development only
		[HttpGet]
		[Route("/api/v1/debug/stop")]
		public ActionResult StopApp()
		{
			Task.Run(async () =>
			{
				await Task.Delay(100);
				ApplicationLifetime.StopApplication();
			});
			
			return new ContentResult { ContentType = "text/plain", StatusCode = (int)HttpStatusCode.OK, Content = "App stopping..." };
		}
		/**/
	}

	/// <summary>
	/// Controller managing account status
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class SecureDebugController : ControllerBase
	{
		/// <summary>
		/// The ACL service singleton
		/// </summary>
		AclService AclService;

		/// <summary>
		/// The database service instance
		/// </summary>
		DatabaseService DatabaseService;

		/// <summary>
		/// The job task source singelton
		/// </summary>
		JobTaskSource JobTaskSource;

		/// <summary>
		/// The globals singleton
		/// </summary>
		ISingletonDocument<Globals> GlobalsSingleton;

		/// <summary>
		/// Collection of pool documents
		/// </summary>
		IPoolCollection PoolCollection;

		/// <summary>
		/// Collection of project documents
		/// </summary>
		IProjectCollection ProjectCollection;

		/// <summary>
		/// Collection of agent documents
		/// </summary>
		IAgentCollection AgentCollection;

		/// <summary>
		/// Collection of session documents
		/// </summary>
		ISessionCollection SessionCollection;

		/// <summary>
		/// Collection of lease documents
		/// </summary>
		ILeaseCollection LeaseCollection;

		/// <summary>
		/// Collection of template documents
		/// </summary>
		ITemplateCollection TemplateCollection;

		/// <summary>
		/// Collection of stream documents
		/// </summary>
		IStreamCollection StreamCollection;

		/// <summary>
		/// The graph collection singleton
		/// </summary>
		IGraphCollection GraphCollection;

		/// <summary>
		/// The log file collection singleton
		/// </summary>
		ILogFileCollection LogFileCollection;

		/// <summary>
		/// The storage provider
		/// </summary>
		IStorageBackend StorageProvider;

		/// <summary>
		/// Perforce client
		/// </summary>
		IPerforceService Perforce;

		/// <summary>
		/// Fleet manager
		/// </summary>
		IFleetManager FleetManager;

		/// <summary>
		/// Settings
		/// </summary>
		private IOptionsMonitor<ServerSettings> Settings;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AclService">The ACL service singleton</param>
		/// <param name="DatabaseService">The database service instance</param>
		/// <param name="JobTaskSource">The dispatch service singleton</param>
		/// <param name="GlobalsSingleton">The globals singleton</param>
		/// <param name="PoolCollection">Collection of pool documents</param>
		/// <param name="ProjectCollection">Collection of project documents</param>
		/// <param name="AgentCollection">Collection of agent documents</param>
		/// <param name="SessionCollection">Collection of session documents</param>
		/// <param name="LeaseCollection">Collection of lease documents</param>
		/// <param name="TemplateCollection">Collection of template documents</param>
		/// <param name="StreamCollection">Collection of stream documents</param>
		/// <param name="GraphCollection">The graph collection</param>
		/// <param name="LogFileCollection">The log file collection</param>
		/// <param name="StorageProvider">The storage provider</param>
		/// <param name="Perforce">Perforce client</param>
		/// <param name="FleetManager">The default fleet manager</param>
		/// <param name="Settings">Settings</param>
		public SecureDebugController(AclService AclService, DatabaseService DatabaseService, JobTaskSource JobTaskSource, ISingletonDocument<Globals> GlobalsSingleton, IPoolCollection PoolCollection, IProjectCollection ProjectCollection, IAgentCollection AgentCollection, ISessionCollection SessionCollection, ILeaseCollection LeaseCollection, ITemplateCollection TemplateCollection, IStreamCollection StreamCollection, IGraphCollection GraphCollection, ILogFileCollection LogFileCollection, IStorageBackend StorageProvider, IPerforceService Perforce, IFleetManager FleetManager, IOptionsMonitor<ServerSettings> Settings)
		{
			this.AclService = AclService;
			this.DatabaseService = DatabaseService;
			this.JobTaskSource = JobTaskSource;
			this.GlobalsSingleton = GlobalsSingleton;
			this.PoolCollection = PoolCollection;
			this.ProjectCollection = ProjectCollection;
			this.AgentCollection = AgentCollection;
			this.SessionCollection = SessionCollection;
			this.LeaseCollection = LeaseCollection;
			this.TemplateCollection = TemplateCollection;
			this.StreamCollection = StreamCollection;
			this.GraphCollection = GraphCollection;
			this.LogFileCollection = LogFileCollection;
			this.StorageProvider = StorageProvider;
			this.Perforce = Perforce;
			this.FleetManager = FleetManager;
			this.Settings = Settings;
		}

		/// <summary>
		/// Prints all the environment variables
		/// </summary>
		/// <returns>Http result</returns>
		[HttpGet]
		[Route("/api/v1/debug/environment")]
		public async Task<ActionResult> GetServerEnvVars()
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			StringBuilder Content = new StringBuilder();
			Content.AppendLine("<html><body><pre>");
			foreach (System.Collections.DictionaryEntry? Pair in System.Environment.GetEnvironmentVariables())
			{
				if (Pair != null)
				{
					Content.AppendLine(HttpUtility.HtmlEncode($"{Pair.Value.Key}={Pair.Value.Value}"));
				}
			}
			Content.Append("</pre></body></html>");
			return new ContentResult { ContentType = "text/html", StatusCode = (int)HttpStatusCode.OK, Content = Content.ToString() };
		}

		/// <summary>
		/// Returns diagnostic information about the current state of the queue
		/// </summary>
		/// <returns>Information about the queue</returns>
		[HttpGet]
		[Route("/api/v1/debug/queue")]
		public async Task<ActionResult<object>> GetQueueStatusAsync()
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			return await JobTaskSource.GetStatus();
		}

		/// <summary>
		/// Returns the complete config Horde uses
		/// </summary>
		/// <returns>Information about the config</returns>
		[HttpGet]
		[Route("/api/v1/debug/config")]
		public async Task<ActionResult> GetConfig()
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			JsonSerializerOptions Options = new JsonSerializerOptions
			{
				WriteIndented = true
			};

			return Ok(JsonSerializer.Serialize(Settings.CurrentValue, Options));
		}

		/// <summary>
		/// Queries for all graphs
		/// </summary>
		/// <returns>The graph definitions</returns>
		[HttpGet]
		[Route("/api/v1/debug/graphs")]
		[ProducesResponseType(200, Type = typeof(GetGraphResponse))]
		public async Task<ActionResult<List<object>>> GetGraphsAsync([FromQuery] int? Index = null, [FromQuery] int? Count = null, [FromQuery] PropertyFilter? Filter = null)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			List<IGraph> Graphs = await GraphCollection.FindAllAsync(null, Index, Count);
			return Graphs.ConvertAll(x => new GetGraphResponse(x).ApplyFilter(Filter));
		}

		/// <summary>
		/// Queries for a particular graph by hash
		/// </summary>
		/// <returns>The graph definition</returns>
		[HttpGet]
		[Route("/api/v1/debug/graphs/{GraphId}")]
		[ProducesResponseType(200, Type = typeof(GetGraphResponse))]
		public async Task<ActionResult<object>> GetGraphAsync(string GraphId, [FromQuery] PropertyFilter? Filter = null)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			IGraph Graph = await GraphCollection.GetAsync(ContentHash.Parse(GraphId));
			return new GetGraphResponse(Graph).ApplyFilter(Filter);
		}

		/// <summary>
		/// Retrieves data from the configured storage provider
		/// </summary>
		/// <param name="Path">Filter for properties to return</param>
		/// <param name="Inline">Whether to show the data inline</param>
		/// <returns>Data from the given path</returns>
		[HttpGet]
		[Route("/api/v1/debug/storage")]
		[ProducesResponseType(typeof(List<GetTemplateResponse>), 200)]
		public async Task<ActionResult<object>> GetRawLogData(string Path, bool Inline = true)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			ReadOnlyMemory<byte>? Data = await StorageProvider.ReadAsync(Path);
			if (Data == null)
			{
				return NotFound();
			}

			Func<Stream, ActionContext, Task> CopyTask = async (OutputStream, Context) => await OutputStream.WriteAsync(Data.Value);
			string MimeType = Inline ? "text/plain" : "application/octet-stream";
			return new CustomFileCallbackResult(System.IO.Path.GetFileName(Path), MimeType, Inline, CopyTask);
		}

		/// <summary>
		/// Query all the job templates.
		/// </summary>
		/// <param name="Filter">Filter for properties to return</param>
		/// <returns>Information about all the job templates</returns>
		[HttpGet]
		[Route("/api/v1/debug/templates")]
		[ProducesResponseType(typeof(List<GetTemplateResponse>), 200)]
		public async Task<ActionResult<object>> GetTemplatesAsync([FromQuery] PropertyFilter? Filter = null)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			List<ITemplate> Templates = await TemplateCollection.FindAllAsync();
			return Templates.ConvertAll(x => new GetTemplateResponse(x).ApplyFilter(Filter));
		}

		/// <summary>
		/// Retrieve information about a specific job template.
		/// </summary>
		/// <param name="TemplateHash">Id of the template to get information about</param>
		/// <param name="Filter">List of properties to return</param>
		/// <returns>Information about the requested template</returns>
		[HttpGet]
		[Route("/api/v1/debug/templates/{TemplateHash}")]
		[ProducesResponseType(typeof(GetTemplateResponse), 200)]
		public async Task<ActionResult<object>> GetTemplateAsync(string TemplateHash, [FromQuery] PropertyFilter? Filter = null)
		{
			ContentHash TemplateHashValue = ContentHash.Parse(TemplateHash);
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			ITemplate? Template = await TemplateCollection.GetAsync(TemplateHashValue);
			if (Template == null)
			{
				return NotFound();
			}
			return Template.ApplyFilter(Filter);
		}

		/// <summary>
		/// Retrieve metadata about a specific log file
		/// </summary>
		/// <param name="LogFileId">Id of the log file to get information about</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>Information about the requested project</returns>
		[HttpGet]
		[Route("/api/v1/debug/logs/{LogFileId}")]
		public async Task<ActionResult<object>> GetLogAsync(string LogFileId, [FromQuery] PropertyFilter? Filter = null)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			ILogFile? LogFile = await LogFileCollection.GetLogFileAsync(LogFileId.ToObjectId());
			if (LogFile == null)
			{
				return NotFound();
			}

			return LogFile.ApplyFilter(Filter);
		}

		/// <summary>
		/// Populate the database with test data
		/// </summary>
		/// <returns>Async task</returns>
		[HttpGet]
		[Route("/api/v1/debug/collections/{Name}")]
		public async Task<ActionResult<object>> GetDocumentsAsync(string Name, [FromQuery] string? Filter = null, [FromQuery] int Index = 0, [FromQuery] int Count = 10)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			IMongoCollection<Dictionary<string, object>> Collection = DatabaseService.GetCollection<Dictionary<string, object>>(Name);
			List<Dictionary<string, object>> Documents = await Collection.Find(Filter ?? "{}").Skip(Index).Limit(Count).ToListAsync();
			return Documents;
		}

		/// <summary>
		/// Create P4 login ticket for the specified username
		/// </summary>
		/// <returns>Perforce ticket</returns>
		[HttpGet]
		[Route("/api/v1/debug/p4ticket")]
		public async Task<ActionResult<string>> CreateTicket([FromQuery] string? Username = null)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			if (Username == null)
			{
				return BadRequest();
			}

			return await Perforce.CreateTicket(Username);
		}

		/// <summary>
		/// Debugs perforce service commands
		/// </summary>
		/// <returns>Perforce ticket</returns>
		[HttpGet]
		[Route("/api/v1/debug/perforce/stress")]
		public async Task<ActionResult<string>> GetPerforceStress()
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminWrite, User))
			{
				return Forbid();
			}

			Dictionary<string, string> Results = new Dictionary<string, string>();
			const string DevBuild = "//UE4/Dev-Build";
			string? PerforceUser = User.GetPerforceUser();

			object UpdateLock = new object();

			// ParallelTest.ForAsync doesn't seem to handle async lambdas
			IEnumerable<Task> Tasks = Enumerable.Range(0, 16).Select(async (Idx) => {

				int LatestChange = await Perforce.GetLatestChangeAsync(DevBuild, PerforceUser);
				int CodeChange = await Perforce.GetCodeChangeAsync(DevBuild, LatestChange);				

				lock (UpdateLock)
				{
					Results.Add($"Parallel Perforce Result {Idx}", $"LatestChange: {LatestChange} - CodeChange: {CodeChange}");
				}
			});

			await Task.WhenAll(Tasks);			

			Tasks = Enumerable.Range(0, 16).Select(async (Idx) => {

				int LatestChange = await Perforce.GetLatestChangeAsync(DevBuild, PerforceUser);
				int CodeChange = await Perforce.GetCodeChangeAsync(DevBuild, LatestChange);				

				lock (UpdateLock)
				{
					Results.Add($"Parallel Perforce Result {Idx + 16}", $"LatestChange: {LatestChange} - CodeChange: {CodeChange}");
				}
			});


			await Task.WhenAll(Tasks);

			StringBuilder Content = new StringBuilder();
			Content.AppendLine("<html><body><pre>");
			foreach (KeyValuePair<string, string> Pair in Results)
			{
				Content.AppendLine(HttpUtility.HtmlEncode($"{Pair.Key}={Pair.Value}"));
			}
			Content.Append("</pre></body></html>");
			return new ContentResult { ContentType = "text/html", StatusCode = (int)HttpStatusCode.OK, Content = Content.ToString() };

		}

		/// <summary>
		/// Debugs perforce service commands
		/// </summary>
		/// <returns>Perforce ticket</returns>
		[HttpGet]
		[Route("/api/v1/debug/perforce/commands")]
		public async Task<ActionResult<string>> GetPerforceCommands()
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminWrite, User))
			{
				return Forbid();
			}

			StringBuilder Content = new StringBuilder();

			const string DevBuild = "//UE4/Dev-Build";
			// a test CL I have setup in Dev-Build
			const int TestCL = 16687685;

			string? PerforceUser = User.GetPerforceUser();

			if (string.IsNullOrEmpty(PerforceUser))
			{
				throw new Exception("Perforce user required");
			}

			Dictionary<string, string> Results = new Dictionary<string, string>();

			// NOTE: This is in Private-Build not DevBuild
			int NewChangeId = await Perforce.CreateNewChangeAsync("//UE4/Private-Build", "Counter.txt");

			List<ChangeDetails> ChangeDetails = await Perforce.GetChangeDetailsAsync("//UE4/Private-Build", new int[] { NewChangeId }, PerforceUser);

			Results.Add("CreateNewChangeAsync", $"{NewChangeId} : {ChangeDetails[0].Description}");

			await Perforce.UpdateChangelistDescription(TestCL, $"Updated Description: New Change CL was {NewChangeId}");

			ChangeDetails = await Perforce.GetChangeDetailsAsync(DevBuild, new int[] { TestCL }, PerforceUser);

			Results.Add("UpdateChangelistDescription", $"{TestCL} : {ChangeDetails[0].Description}");

			int LatestChange = await Perforce.GetLatestChangeAsync(DevBuild, PerforceUser);
			Results.Add("GetLatestChangeAsync", LatestChange.ToString(CultureInfo.InvariantCulture));

			int CodeChange = await Perforce.GetCodeChangeAsync(DevBuild, LatestChange);
			Results.Add("GetCodeChangeAsync", CodeChange.ToString(CultureInfo.InvariantCulture));

			PerforceUserInfo? UserInfo = await Perforce.GetUserInfoAsync(PerforceUser);

			if (UserInfo == null)
			{
				Results.Add("GetUserInfoAsync", "null");
			}
			else
			{
				Results.Add("GetUserInfoAsync", $"{UserInfo.Name} : {UserInfo.Email}");
			}

			// changes
			List<ChangeSummary> Changes = await Perforce.GetChangesAsync(DevBuild, null, LatestChange, 10, PerforceUser);
			int Count = 0;
			foreach (ChangeSummary Summary in Changes)
			{
				Results.Add($"GetChangesAsync Result {Count++}", $"{Summary.Number} : {Summary.Author} - {Summary.Description}");
			}

			ChangeDetails = await Perforce.GetChangeDetailsAsync(DevBuild, Changes.Select(Change => Change.Number).ToArray(), PerforceUser);
			Count = 0;
			foreach (ChangeDetails Details in ChangeDetails)
			{
				Results.Add($"GetChangeDetailsAsync Result {Count++}", $"{Details.Number} : {Details.Author} - {Details.Files.ToJson()}");
			}

			String Ticket = "Failed (Expected if not running service account)";
			try
			{
				await Perforce.CreateTicket(PerforceUser);
				Ticket = "Success";
			}
			catch
			{

			}

			Results.Add($"CreateTicket", $"{Ticket}");
			List<FileSummary> FileSummaries = await Perforce.FindFilesAsync(ChangeDetails.SelectMany(Details => Details.Files).Select(File => $"{DevBuild}{File}"));
			Count = 0;
			foreach (FileSummary File in FileSummaries)
			{
				Results.Add($"FindFilesAsync Result {Count++}", $"{File.Change} : {File.DepotPath} : {File.Exists}");
			}

			// print async

			byte[] Bytes = await Perforce.PrintAsync($"{DevBuild}/RunUAT.bat");
			Results.Add($"PrintAsync: {DevBuild}/RunUAT.bat ", $"{System.Text.Encoding.Default.GetString(Bytes)}");

			Bytes = await Perforce.PrintAsync($"{DevBuild}/RunUAT.bat@13916380");
			Results.Add($"PrintAsync: {DevBuild}/RunUAT.bat@13916380", $"{System.Text.Encoding.Default.GetString(Bytes)}");

			// DuplicateShelvedChangeAsync is disabled due to issue with p4 librarian
			/*
			// need to be running as service account for these

			int? DuplicateCL = null;

			int ShelvedChangeOnDevBuild = 16158333;

			try 
			{
				DuplicateCL = await Perforce.DuplicateShelvedChangeAsync(ShelvedChangeOnDevBuild);
			}
			catch 
			{
				Results.Add($"DuplicateShelvedChangeAsync",  "Failed - expected unless using service account");
			}

			if (DuplicateCL.HasValue)
			{
				// Note change client much exist for the update
				ChangeDetails = await Perforce.GetChangeDetailsAsync(DevBuild, new int[]{NewChangeId}, PerforceUser);

				Results.Add("UpdateChangelistDescription - PreUpdate", $"{DuplicateCL} : {ChangeDetails[0].Description}"); 

				try 
				{
					await Perforce.UpdateChangelistDescription(DuplicateCL.Value, "Updated Description from Horde");

					ChangeDetails = await Perforce.GetChangeDetailsAsync(DevBuild, new int[]{NewChangeId}, PerforceUser);

					Results.Add("UpdateChangelistDescription - PostUpdate", $"{DuplicateCL} : {ChangeDetails[0].Description}"); 
				}
				catch 
				{
					Results.Add("UpdateChangelistDescription - Faile", $"Client from change {DuplicateCL} must exist"); 
				}

				await Perforce.DeleteShelvedChangeAsync(DuplicateCL.Value);

				ChangeDetails = await Perforce.GetChangeDetailsAsync(DevBuild,new int[] {DuplicateCL.Value} , PerforceUser);

				Results.Add("GetChangeDetailsAsync - After Delete", $"ChangeDetails.Count: {ChangeDetails.Count}"); 

			}
			*/

			byte[] ImageBytes = await Perforce.PrintAsync($"{DevBuild}/Samples/Games/ShooterGame/ShooterGame.png");


			Content.AppendLine("<html><body><pre>");
			foreach (KeyValuePair<string, string> Pair in Results)
			{
				Content.AppendLine(HttpUtility.HtmlEncode($"{Pair.Key}={Pair.Value}"));
			}

			Content.Append($"PrintAsync Binary: {DevBuild}/Samples/Games/ShooterGame/ShooterGame.png\n</pre>");
			Content.Append($"<img src=\"data:image/png;base64,{Convert.ToBase64String(ImageBytes, Base64FormattingOptions.None)}\"/>");

			Content.Append("</body></html>");
			return new ContentResult { ContentType = "text/html", StatusCode = (int)HttpStatusCode.OK, Content = Content.ToString() };

		}

		/// <summary>
		/// Simulates a crash of the native p4 bridge
		/// </summary>		
		[HttpGet]		
		[Route("/api/v1/debug/perforce/debugcrash")]		
		public async Task<ActionResult> GetPerforceDebugCrash()
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminWrite, User))
			{
				return Forbid();
			}

			P4Debugging.DebugCrash();

			return Ok();
		}

		/// <summary>
		/// Debugging fleet managers
		/// </summary>
		/// <returns>Nothing</returns>
		[HttpGet]
		[Route("/api/v1/debug/fleetmanager")]
		public async Task<ActionResult<string>> FleetManage([FromQuery] string? PoolId = null, [FromQuery] string? ChangeType = null, [FromQuery] int? Count = null)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminRead, User))
			{
				return Forbid();
			}

			if (PoolId == null)
			{
				return BadRequest("Missing 'PoolId' query parameter");
			}

			IPool? Pool = await PoolCollection.GetAsync(new PoolId(PoolId));
			if (Pool == null)
			{
				return BadRequest($"Pool with ID '{PoolId}' not found");
			}

			if (!(ChangeType == "expand" || ChangeType == "shrink"))
			{
				return BadRequest("Missing 'ChangeType' query parameter and/or must be set to 'expand' or 'shrink'");
			}

			if (Count == null || Count.Value <= 0)
			{
				return BadRequest("Missing 'Count' query parameter and/or must be greater than 0");
			}

			List<IAgent> Agents = new List<IAgent>();
			string Message = "No operation";

			if (ChangeType == "expand")
			{
				await FleetManager.ExpandPool(Pool, Agents, Count.Value);
				Message = $"Expanded pool {Pool.Name} by {Count}";
			}
			else if (ChangeType == "shrink")
			{
				Agents = await AgentCollection.FindAsync();
				Agents = Agents.FindAll(a => a.InPool(Pool)).ToList();
				await FleetManager.ShrinkPool(Pool, Agents, Count.Value);
				Message = $"Shrank pool {Pool.Name} by {Count}";
			}

			return new ContentResult { ContentType = "text/plain", StatusCode = (int)HttpStatusCode.OK, Content = Message };
		}
	}
}
