

CREATE Procedure [dbo].[sp_delete_guild_region]
	@RegionID int,
    @GuNum int,
	@nReturn int	OUTPUT	
AS
	DECLARE @error_var int, 
		    @rowcount_var int

	SET NOCOUNT ON
		 
	SET @nReturn = 0	
	SET @error_var = 0
	SET @rowcount_var = 0
	
	IF EXISTS(SELECT * FROM GuildRegion WHERE RegionID=@RegionID AND GuNum=@GuNum)
	BEGIN -- 瘤开苞 辨靛啊 粮犁窍搁
		UPDATE GuildRegion 
		SET GuNum=0, RegionTax=0 
		WHERE RegionID=@RegionID
		
		SELECT @error_var = @@ERROR, @rowcount_var = @@ROWCOUNT
		IF @error_var <> 0 OR @rowcount_var = 0 -- 俊矾惯积
		BEGIN
			SET @nReturn = 0
			SET NOCOUNT OFF
			RETURN @nReturn
		END
	END
	ELSE -- 瘤开苞 辨靛啊 粮犁窍瘤 臼栏搁 俊矾
	BEGIN
		SET @nReturn = 0
		SET NOCOUNT OFF			
		RETURN @nReturn		
	END
	
	SET @nReturn = 1
	SET NOCOUNT OFF
	RETURN @nReturn


